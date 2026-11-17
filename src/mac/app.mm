// Super A'Can Emulator — native macOS frontend (AppKit + CoreAudio + GameController)
// Theme: console gray + A'Can green.

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <AudioToolbox/AudioToolbox.h>
#import <GameController/GameController.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <os/lock.h>
#include <memory>
#include <vector>
#include <algorithm>
#include "emu.h"
#include "savestate.h"

// ─────────────────────────────────────────────────────────────────────────────
#pragma mark - Theme

static NSColor *HEX(uint32_t h, CGFloat a = 1.0) {
    return [NSColor colorWithSRGBRed:((h >> 16) & 0xFF) / 255.0
                               green:((h >> 8) & 0xFF) / 255.0
                                blue:(h & 0xFF) / 255.0
                               alpha:a];
}
namespace T {
    static const uint32_t bg        = 0x1C1F22; // window background
    static const uint32_t panel     = 0x25292D; // header / sidebar / footer
    static const uint32_t card      = 0x2E3338;
    static const uint32_t cardHover = 0x363C42;
    static const uint32_t border    = 0x3A4046;
    static const uint32_t text      = 0xE8ECEF;
    static const uint32_t text2     = 0x939DA6;
    static const uint32_t console   = 0xB4B9BD; // console shell gray
    static const uint32_t consoleDk = 0x8C9296;
    static const uint32_t green     = 0x34B04A; // A'Can green
    static const uint32_t greenHi   = 0x4CC862;
    static const uint32_t greenDk   = 0x23813A;
    static const uint32_t gameBg    = 0x121416;
    static const uint32_t danger    = 0xD9604C;
}

static NSFont *F(CGFloat size, NSFontWeight w = NSFontWeightRegular) {
    return [NSFont systemFontOfSize:size weight:w];
}

static void DrawText(NSString *s, NSRect r, NSFont *font, NSColor *color,
                     NSTextAlignment align = NSTextAlignmentLeft, CGFloat kern = 0) {
    NSMutableParagraphStyle *p = [NSMutableParagraphStyle new];
    p.alignment = align;
    p.lineBreakMode = NSLineBreakByTruncatingTail;
    NSMutableDictionary *attrs = [@{ NSFontAttributeName: font,
                                     NSForegroundColorAttributeName: color,
                                     NSParagraphStyleAttributeName: p } mutableCopy];
    if (kern != 0) attrs[NSKernAttributeName] = @(kern);
    [s drawWithRect:r
            options:NSStringDrawingUsesLineFragmentOrigin | NSStringDrawingTruncatesLastVisibleLine
         attributes:attrs];
}

static NSSize TextSize(NSString *s, NSFont *font, CGFloat kern = 0) {
    NSMutableDictionary *a = [@{ NSFontAttributeName: font } mutableCopy];
    if (kern != 0) a[NSKernAttributeName] = @(kern);
    return [s sizeWithAttributes:a];
}

static NSString *Initials(NSString *title) {
    NSMutableString *out = [NSMutableString string];
    for (NSString *w in [title componentsSeparatedByCharactersInSet:NSCharacterSet.whitespaceCharacterSet]) {
        if (w.length == 0) continue;
        unichar c = [w characterAtIndex:0];
        if (![NSCharacterSet.alphanumericCharacterSet characterIsMember:c]) continue;
        [out appendFormat:@"%C", (unichar)toupper(c)];
        if (out.length == 2) break;
    }
    return out.length ? out : @"?";
}

// Draws the stylised Super A'Can cartridge used as placeholder art.
static void DrawCartridge(NSRect area, NSString *title, CGFloat alpha = 1.0) {
    CGFloat h = area.size.height * 0.78;
    CGFloat w = h * 0.82;
    if (w > area.size.width * 0.7) { w = area.size.width * 0.7; h = w / 0.82; }
    NSRect cart = NSMakeRect(NSMidX(area) - w / 2, NSMidY(area) - h / 2, w, h);

    // Shadow
    NSBezierPath *shadow = [NSBezierPath bezierPathWithRoundedRect:NSOffsetRect(cart, 0, h * 0.03)
                                                           xRadius:w * 0.06 yRadius:w * 0.06];
    [HEX(0x000000, 0.35 * alpha) setFill]; [shadow fill];

    // Shell (console gray)
    NSBezierPath *shell = [NSBezierPath bezierPathWithRoundedRect:cart xRadius:w * 0.06 yRadius:w * 0.06];
    NSGradient *g = [[NSGradient alloc] initWithStartingColor:HEX(0xC6CACD, alpha)
                                                  endingColor:HEX(T::console, alpha)];
    [g drawInBezierPath:shell angle:90];

    // Grip ridges at top
    [HEX(T::consoleDk, alpha) setFill];
    for (int i = 0; i < 3; i++) {
        NSRect ridge = NSMakeRect(cart.origin.x + w * 0.18, cart.origin.y + h * (0.06 + i * 0.045),
                                  w * 0.64, h * 0.018);
        [[NSBezierPath bezierPathWithRoundedRect:ridge xRadius:1 yRadius:1] fill];
    }

    // Green label
    NSRect label = NSMakeRect(cart.origin.x + w * 0.1, cart.origin.y + h * 0.26, w * 0.8, h * 0.56);
    NSBezierPath *lp = [NSBezierPath bezierPathWithRoundedRect:label xRadius:w * 0.04 yRadius:w * 0.04];
    NSGradient *lg = [[NSGradient alloc] initWithStartingColor:HEX(T::greenHi, alpha)
                                                   endingColor:HEX(T::greenDk, alpha)];
    [lg drawInBezierPath:lp angle:90];

    // Label stripe + initials
    [HEX(0xFFFFFF, 0.18 * alpha) setFill];
    NSRectFill(NSMakeRect(label.origin.x, label.origin.y + label.size.height * 0.16,
                          label.size.width, label.size.height * 0.05));
    NSFont *f = [NSFont systemFontOfSize:label.size.height * 0.36 weight:NSFontWeightHeavy];
    NSString *ini = Initials(title ?: @"");
    NSSize ts = TextSize(ini, f);
    DrawText(ini, NSMakeRect(label.origin.x, NSMidY(label) - ts.height / 2 + label.size.height * 0.06,
                             label.size.width, ts.height),
             f, HEX(0xFFFFFF, 0.95 * alpha), NSTextAlignmentCenter);

    // Bottom connector slot
    [HEX(T::consoleDk, alpha) setFill];
    NSRectFill(NSMakeRect(cart.origin.x + w * 0.2, NSMaxY(cart) - h * 0.07, w * 0.6, h * 0.025));
}

// ─────────────────────────────────────────────────────────────────────────────
#pragma mark - Paths

static NSURL *SupportDir(NSString *sub = nil) {
    NSURL *base = [[NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory
                                                        inDomains:NSUserDomainMask].firstObject
                   URLByAppendingPathComponent:@"Super ACan Emulator" isDirectory:YES];
    if (sub) base = [base URLByAppendingPathComponent:sub isDirectory:YES];
    [NSFileManager.defaultManager createDirectoryAtURL:base withIntermediateDirectories:YES attributes:nil error:nil];
    return base;
}

// ─────────────────────────────────────────────────────────────────────────────
#pragma mark - Game model

@interface SCGame : NSObject
@property (copy) NSString *path;
@property (copy) NSString *title;
@property (strong) NSDate *lastPlayed;
@property (strong) NSImage *cover;
@property (readonly) NSString *identifier;
@property (readonly) BOOL exists;
@end

@implementation SCGame
+ (NSString *)titleForPath:(NSString *)path {
    NSString *name = path.lastPathComponent.stringByDeletingPathExtension;
    NSString *lower = name.lowercaseString;
    BOOL english = [lower containsString:@"_en"] || [lower containsString:@"english"] || [lower containsString:@"(en)"];
    if ([lower containsString:@"16007"] || [lower containsString:@"dragon-force"] ||
        [lower containsString:@"dragon force"] || [lower containsString:@"dragonforce"]) {
        return english ? @"Super Dragon Force (English)" : @"Super Dragon Force";
    }
    NSRegularExpression *paren = [NSRegularExpression regularExpressionWithPattern:@"\\s*[\\(\\[][^\\)\\]]*[\\)\\]]" options:0 error:nil];
    name = [paren stringByReplacingMatchesInString:name options:0 range:NSMakeRange(0, name.length) withTemplate:@""];
    name = [name stringByReplacingOccurrencesOfString:@"_" withString:@" "];
    name = [name stringByReplacingOccurrencesOfString:@"-" withString:@" "];
    while ([name containsString:@"  "]) name = [name stringByReplacingOccurrencesOfString:@"  " withString:@" "];
    name = [name stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
    return name.length ? name : path.lastPathComponent;
}
- (NSString *)identifier {
    NSString *base = self.path.lastPathComponent.stringByDeletingPathExtension;
    NSCharacterSet *bad = [[NSCharacterSet alphanumericCharacterSet] invertedSet];
    NSString *clean = [[base componentsSeparatedByCharactersInSet:bad] componentsJoinedByString:@"_"];
    // Stable FNV-1a hash of the path (NSString.hash isn't guaranteed stable across launches).
    uint32_t h = 2166136261u;
    for (const char *c = self.path.UTF8String; *c; c++) { h ^= (uint8_t)*c; h *= 16777619u; }
    return [NSString stringWithFormat:@"%@_%08x", clean, h];
}
- (BOOL)exists { return [NSFileManager.defaultManager fileExistsAtPath:self.path]; }
- (NSURL *)coverURL { return [SupportDir(@"Covers") URLByAppendingPathComponent:[self.identifier stringByAppendingString:@".png"]]; }
- (NSURL *)stateURL:(int)slot {
    return [SupportDir(@"States") URLByAppendingPathComponent:
            [NSString stringWithFormat:@"%@.slot%d.state", self.identifier, slot]];
}
- (void)loadCover { self.cover = [[NSImage alloc] initWithContentsOfURL:[self coverURL]]; }
- (NSURL *)batterySaveURL {
    return [SupportDir(@"Saves") URLByAppendingPathComponent:[self.identifier stringByAppendingString:@".sav"]];
}
- (NSDictionary *)dict {
    NSMutableDictionary *d = [@{ @"path": self.path, @"title": self.title } mutableCopy];
    if (self.lastPlayed) d[@"lastPlayed"] = self.lastPlayed;
    return d;
}
+ (instancetype)gameWithDict:(NSDictionary *)d {
    SCGame *g = [SCGame new];
    g.path = d[@"path"]; g.title = d[@"title"] ?: [SCGame titleForPath:g.path];
    g.lastPlayed = d[@"lastPlayed"];
    [g loadCover];
    return g;
}
@end

static NSString *RelativeDate(NSDate *d) {
    if (!d) return @"Never played";
    NSRelativeDateTimeFormatter *f = [NSRelativeDateTimeFormatter new];
    f.unitsStyle = NSRelativeDateTimeFormatterUnitsStyleFull;
    if (fabs(d.timeIntervalSinceNow) < 60) return @"Played just now";
    return [@"Played " stringByAppendingString:[f localizedStringForDate:d relativeToDate:[NSDate date]]];
}

// ─────────────────────────────────────────────────────────────────────────────
#pragma mark - Audio (CoreAudio AudioQueue + ring buffer)

class AudioOut {
public:
    static constexpr double kRate = 44744.0;
    ~AudioOut() { shutdown(); }

    bool start() {
        if (!queue) {
            AudioStreamBasicDescription f = {};
            f.mSampleRate = kRate;
            f.mFormatID = kAudioFormatLinearPCM;
            f.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
            f.mChannelsPerFrame = 2;
            f.mBitsPerChannel = 16;
            f.mBytesPerFrame = 4;
            f.mFramesPerPacket = 1;
            f.mBytesPerPacket = 4;
            if (AudioQueueNewOutput(&f, &AudioOut::callback, this, nullptr, nullptr, 0, &queue) != noErr) {
                queue = nullptr;
                return false;
            }
            ring.assign(static_cast<size_t>(kRate) * 2 / 2, 0); // 0.5 s stereo
            for (int i = 0; i < 4; i++) {
                AudioQueueBufferRef b;
                AudioQueueAllocateBuffer(queue, 512 * 4, &b);
                fill(b);
                AudioQueueEnqueueBuffer(queue, b, 0, nullptr);
            }
        }
        clear();
        AudioQueueSetParameter(queue, kAudioQueueParam_Volume, muted ? 0.0f : 1.0f);
        return AudioQueueStart(queue, nullptr) == noErr;
    }
    void pause() { if (queue) AudioQueuePause(queue); }
    void shutdown() { if (queue) { AudioQueueStop(queue, true); AudioQueueDispose(queue, true); queue = nullptr; } }
    void setMuted(bool m) { muted = m; if (queue) AudioQueueSetParameter(queue, kAudioQueueParam_Volume, m ? 0.0f : 1.0f); }
    bool isMuted() const { return muted; }
    bool ok() const { return queue != nullptr; }

    void push(const int16_t *s, size_t n) {
        os_unfair_lock_lock(&lock);
        size_t cap = ring.size();
        for (size_t i = 0; i < n; i++) {
            if (count == cap) { rd = (rd + 1) % cap; count--; }
            ring[wr] = s[i]; wr = (wr + 1) % cap; count++;
        }
        os_unfair_lock_unlock(&lock);
    }
    size_t queuedFrames() {
        os_unfair_lock_lock(&lock); size_t c = count; os_unfair_lock_unlock(&lock);
        return c / 2;
    }
    void clear() { os_unfair_lock_lock(&lock); rd = wr = count = 0; os_unfair_lock_unlock(&lock); }

private:
    static void callback(void *user, AudioQueueRef q, AudioQueueBufferRef b) {
        auto *self = static_cast<AudioOut *>(user);
        self->fill(b);
        AudioQueueEnqueueBuffer(q, b, 0, nullptr);
    }
    void fill(AudioQueueBufferRef b) {
        int16_t *out = static_cast<int16_t *>(b->mAudioData);
        size_t want = b->mAudioDataBytesCapacity / 2;
        os_unfair_lock_lock(&lock);
        size_t cap = ring.size(), i = 0;
        for (; i < want && count > 0; i++) { out[i] = ring[rd]; rd = (rd + 1) % cap; count--; }
        os_unfair_lock_unlock(&lock);
        for (; i < want; i++) out[i] = 0;
        b->mAudioDataByteSize = static_cast<UInt32>(want * 2);
    }
    AudioQueueRef queue = nullptr;
    std::vector<int16_t> ring;
    size_t rd = 0, wr = 0, count = 0;
    os_unfair_lock lock = OS_UNFAIR_LOCK_INIT;
    bool muted = false;
};

// ─────────────────────────────────────────────────────────────────────────────
#pragma mark - Controller protocol

@protocol SCHost <NSObject>
- (void)playGame:(SCGame *)g;
- (void)selectGame:(SCGame *)g;
- (NSMenu *)menuForGame:(SCGame *)g;
- (void)addROMs:(id)sender;
- (void)setFilter:(NSInteger)filter;
- (NSInteger)filter;
- (NSInteger)countForFilter:(NSInteger)filter;
- (void)gameKey:(NSEvent *)e down:(BOOL)down;
- (void)addURLs:(NSArray<NSURL *> *)urls play:(BOOL)play;
- (BOOL)inGame;
@end

// ─────────────────────────────────────────────────────────────────────────────
#pragma mark - Button

typedef NS_ENUM(NSInteger, SCButtonStyle) { SCButtonPrimary, SCButtonSecondary, SCButtonGhost };

@interface SCButton : NSButton
@property SCButtonStyle style;
@property BOOL hover;
@property (strong) NSTrackingArea *area;
+ (instancetype)withTitle:(NSString *)t style:(SCButtonStyle)s target:(id)target action:(SEL)a;
@end

@implementation SCButton
+ (instancetype)withTitle:(NSString *)t style:(SCButtonStyle)s target:(id)target action:(SEL)a {
    SCButton *b = [[SCButton alloc] initWithFrame:NSMakeRect(0, 0, 120, 34)];
    b.title = t; b.style = s; b.target = target; b.action = a;
    b.bordered = NO; b.focusRingType = NSFocusRingTypeNone; b.refusesFirstResponder = YES;
    return b;
}
- (NSSize)intrinsicContentSize {
    NSSize s = TextSize(self.title, F(13, NSFontWeightSemibold));
    return NSMakeSize(ceil(s.width) + 32, 34);
}
- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (self.area) [self removeTrackingArea:self.area];
    self.area = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                             options:NSTrackingMouseEnteredAndExited | NSTrackingActiveInActiveApp | NSTrackingInVisibleRect
                                               owner:self userInfo:nil];
    [self addTrackingArea:self.area];
}
- (void)mouseEntered:(NSEvent *)e { self.hover = YES; self.needsDisplay = YES; }
- (void)mouseExited:(NSEvent *)e { self.hover = NO; self.needsDisplay = YES; }
- (void)resetCursorRects { if (self.enabled) [self addCursorRect:self.bounds cursor:NSCursor.pointingHandCursor]; }
- (void)drawRect:(NSRect)dirty {
    NSRect r = NSInsetRect(self.bounds, 0.5, 0.5);
    BOOL down = self.isHighlighted, on = self.enabled;
    NSColor *fill, *stroke = nil, *fg;
    switch (self.style) {
        case SCButtonPrimary:
            fill = HEX(down ? T::greenDk : (self.hover ? T::greenHi : T::green));
            fg = HEX(0xFFFFFF);
            break;
        case SCButtonSecondary:
            fill = HEX(down ? T::border : (self.hover ? T::cardHover : T::card));
            stroke = HEX(self.hover ? 0x4B535A : T::border);
            fg = HEX(T::text);
            break;
        default:
            fill = self.hover ? HEX(0xFFFFFF, down ? 0.10 : 0.06) : NSColor.clearColor;
            fg = HEX(self.hover ? T::text : T::text2);
            break;
    }
    if (!on) { fill = [fill colorWithAlphaComponent:fill.alphaComponent * 0.35]; fg = [fg colorWithAlphaComponent:0.4]; }
    NSBezierPath *p = [NSBezierPath bezierPathWithRoundedRect:r xRadius:8 yRadius:8];
    [fill setFill]; [p fill];
    if (stroke) { [stroke setStroke]; p.lineWidth = 1; [p stroke]; }
    NSFont *f = F(13, NSFontWeightSemibold);
    NSSize ts = TextSize(self.title, f);
    DrawText(self.title, NSMakeRect(0, (self.bounds.size.height - ts.height) / 2, self.bounds.size.width, ts.height),
             f, fg, NSTextAlignmentCenter);
}
@end

// ─────────────────────────────────────────────────────────────────────────────
#pragma mark - Header

@interface SCHeaderView : NSView
@property (strong) NSSearchField *search;
@property (strong) SCButton *addButton;
@end

@implementation SCHeaderView
- (BOOL)isFlipped { return YES; }
- (BOOL)mouseDownCanMoveWindow { return YES; }
- (void)layout {
    [super layout];
    CGFloat W = self.bounds.size.width, H = self.bounds.size.height;
    NSSize bs = self.addButton.intrinsicContentSize;
    self.addButton.frame = NSMakeRect(W - bs.width - 20, (H - 34) / 2, bs.width, 34);
    CGFloat sw = MIN(260, MAX(140, W * 0.25));
    self.search.frame = NSMakeRect(NSMinX(self.addButton.frame) - sw - 12, (H - 28) / 2, sw, 28);
}
- (void)drawRect:(NSRect)dirty {
    [HEX(T::panel) setFill]; NSRectFill(self.bounds);
    [HEX(T::border) setFill];
    NSRectFill(NSMakeRect(0, self.bounds.size.height - 1, self.bounds.size.width, 1));

    // Logo: green badge + wordmark, placed after the window traffic lights.
    CGFloat x = 86, cy = self.bounds.size.height / 2;
    NSRect badge = NSMakeRect(x, cy - 16, 32, 32);
    NSGradient *g = [[NSGradient alloc] initWithStartingColor:HEX(T::greenHi) endingColor:HEX(T::greenDk)];
    [g drawInBezierPath:[NSBezierPath bezierPathWithRoundedRect:badge xRadius:8 yRadius:8] angle:90];
    NSFont *bf = F(15, NSFontWeightBlack);
    NSSize bs = TextSize(@"A'", bf);
    DrawText(@"A'", NSMakeRect(badge.origin.x, cy - bs.height / 2, 32, bs.height), bf, HEX(0xFFFFFF), NSTextAlignmentCenter);

    NSFont *wf = F(17, NSFontWeightHeavy);
    CGFloat tx = NSMaxX(badge) + 11;
    NSSize s1 = TextSize(@"SUPER", wf, 1.2);
    DrawText(@"SUPER", NSMakeRect(tx, cy - 17, s1.width + 2, 22), wf, HEX(T::console), NSTextAlignmentLeft, 1.2);
    DrawText(@"A'CAN", NSMakeRect(tx + s1.width + 7, cy - 17, 80, 22), wf, HEX(T::green), NSTextAlignmentLeft, 1.2);
    DrawText(@"EMULATOR", NSMakeRect(tx + 1, cy + 4, 140, 14), F(9, NSFontWeightSemibold), HEX(T::text2),
             NSTextAlignmentLeft, 2.6);
}
@end

// ─────────────────────────────────────────────────────────────────────────────
#pragma mark - Sidebar

@interface SCSidebarView : NSView
@property (weak) id<SCHost> host;
@end

@implementation SCSidebarView
- (BOOL)isFlipped { return YES; }
- (NSRect)rowRect:(NSInteger)i { return NSMakeRect(10, 44 + i * 36, self.bounds.size.width - 20, 32); }
- (void)mouseDown:(NSEvent *)e {
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    for (NSInteger i = 0; i < 2; i++) if (NSPointInRect(p, [self rowRect:i])) { [self.host setFilter:i]; }
}
- (void)drawRect:(NSRect)dirty {
    NSRect b = self.bounds;
    [HEX(T::panel) setFill]; NSRectFill(b);
    [HEX(T::border) setFill]; NSRectFill(NSMakeRect(b.size.width - 1, 0, 1, b.size.height));

    DrawText(@"LIBRARY", NSMakeRect(20, 20, 160, 14), F(10, NSFontWeightBold), HEX(T::text2), NSTextAlignmentLeft, 1.4);
    NSArray *names = @[@"All Games", @"Recently Played"];
    NSArray *icons = @[@"square.grid.2x2.fill", @"clock.fill"];
    for (NSInteger i = 0; i < 2; i++) {
        NSRect r = [self rowRect:i];
        BOOL sel = (self.host.filter == i);
        if (sel) {
            [HEX(T::card) setFill];
            [[NSBezierPath bezierPathWithRoundedRect:r xRadius:7 yRadius:7] fill];
            [HEX(T::green) setFill];
            [[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(r.origin.x, r.origin.y + 7, 3, r.size.height - 14)
                                             xRadius:1.5 yRadius:1.5] fill];
        }
        NSImage *icon = [NSImage imageWithSystemSymbolName:icons[i] accessibilityDescription:nil];
        if (icon) {
            NSImageSymbolConfiguration *cfg = [NSImageSymbolConfiguration configurationWithPointSize:12 weight:NSFontWeightSemibold];
            cfg = [cfg configurationByApplyingConfiguration:[NSImageSymbolConfiguration configurationWithHierarchicalColor:
                                                             HEX(sel ? T::green : T::text2)]];
            icon = [icon imageWithSymbolConfiguration:cfg];
            NSSize is = icon.size;
            [icon drawInRect:NSMakeRect(r.origin.x + 14, NSMidY(r) - is.height / 2, is.width, is.height)
                    fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:nil];
        }
        DrawText(names[i], NSMakeRect(r.origin.x + 38, r.origin.y + 8, r.size.width - 80, 18),
                 F(13, sel ? NSFontWeightSemibold : NSFontWeightRegular), HEX(sel ? T::text : 0xB7C0C7));
        DrawText([NSString stringWithFormat:@"%ld", (long)[self.host countForFilter:i]],
                 NSMakeRect(NSMaxX(r) - 44, r.origin.y + 9, 34, 16), F(11, NSFontWeightMedium), HEX(T::text2),
                 NSTextAlignmentRight);
    }

    // Controls reference card
    NSArray *rows = @[@[@"↑ ↓ ← →", @"D-Pad"], @[@"Z  X", @"A  B"], @[@"A  S", @"X  Y"],
                      @[@"Q  W", @"L  R"], @[@"Return", @"Start"], @[@"Space", @"Select"],
                      @[@"Tab", @"Fast-forward"], @[@"Esc", @"Pause menu"]];
    CGFloat ch = 40 + rows.count * 20 + 10;
    NSRect card = NSMakeRect(12, b.size.height - ch - 14, b.size.width - 24, ch);
    if (card.origin.y < 130) return;
    [HEX(T::bg) setFill];
    [[NSBezierPath bezierPathWithRoundedRect:card xRadius:10 yRadius:10] fill];
    DrawText(@"CONTROLS", NSMakeRect(card.origin.x + 14, card.origin.y + 14, 120, 14),
             F(10, NSFontWeightBold), HEX(T::text2), NSTextAlignmentLeft, 1.4);
    for (NSUInteger i = 0; i < rows.count; i++) {
        CGFloat y = card.origin.y + 38 + i * 20;
        NSString *key = rows[i][0];
        NSFont *kf = [NSFont monospacedSystemFontOfSize:10.5 weight:NSFontWeightSemibold];
        NSSize ks = TextSize(key, kf);
        NSRect kr = NSMakeRect(card.origin.x + 14, y, ks.width + 10, 16);
        [HEX(T::card) setFill];
        [[NSBezierPath bezierPathWithRoundedRect:kr xRadius:4 yRadius:4] fill];
        DrawText(key, NSMakeRect(kr.origin.x, y + 1.5, kr.size.width, 14), kf, HEX(T::console), NSTextAlignmentCenter);
        DrawText(rows[i][1], NSMakeRect(card.origin.x + 92, y + 1, card.size.width - 100, 15),
                 F(11.5), HEX(0xB7C0C7));
    }
}
@end

// ─────────────────────────────────────────────────────────────────────────────
#pragma mark - Game card

@interface SCCardView : NSView
@property (strong) SCGame *game;
@property (weak) id<SCHost> host;
@property BOOL selected, hover;
@property (strong) NSTrackingArea *area;
@end

@implementation SCCardView
- (BOOL)isFlipped { return YES; }
- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (self.area) [self removeTrackingArea:self.area];
    self.area = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                             options:NSTrackingMouseEnteredAndExited | NSTrackingActiveInActiveApp | NSTrackingInVisibleRect
                                               owner:self userInfo:nil];
    [self addTrackingArea:self.area];
}
- (void)mouseEntered:(NSEvent *)e { self.hover = YES; self.needsDisplay = YES; }
- (void)mouseExited:(NSEvent *)e { self.hover = NO; self.needsDisplay = YES; }
- (void)mouseDown:(NSEvent *)e {
    [self.host selectGame:self.game];
    if (e.clickCount == 2) [self.host playGame:self.game];
}
- (NSMenu *)menuForEvent:(NSEvent *)e { [self.host selectGame:self.game]; return [self.host menuForGame:self.game]; }

- (NSRect)thumbRect {
    CGFloat w = self.bounds.size.width - 16;
    return NSMakeRect(8, 8, w, round(w * 0.75));
}
- (void)drawRect:(NSRect)dirty {
    NSRect b = NSInsetRect(self.bounds, 1, 1);
    NSBezierPath *bg = [NSBezierPath bezierPathWithRoundedRect:b xRadius:12 yRadius:12];
    [HEX(self.hover || self.selected ? T::cardHover : T::card) setFill]; [bg fill];

    NSRect tr = [self thumbRect];
    [NSGraphicsContext saveGraphicsState];
    [[NSBezierPath bezierPathWithRoundedRect:tr xRadius:7 yRadius:7] addClip];
    NSGradient *tg = [[NSGradient alloc] initWithStartingColor:HEX(0x2A2F34) endingColor:HEX(0x1E2226)];
    [tg drawInRect:tr angle:90];
    if (self.game.cover) {
        NSSize is = self.game.cover.size;
        CGFloat scale = MAX(tr.size.width / is.width, tr.size.height / is.height);
        NSSize ds = NSMakeSize(is.width * scale, is.height * scale);
        NSRect dr = NSMakeRect(NSMidX(tr) - ds.width / 2, NSMidY(tr) - ds.height / 2, ds.width, ds.height);
        [self.game.cover drawInRect:dr fromRect:NSZeroRect operation:NSCompositingOperationSourceOver
                           fraction:self.game.exists ? 1.0 : 0.35 respectFlipped:YES
                              hints:@{ NSImageHintInterpolation: @(NSImageInterpolationNone) }];
    } else {
        DrawCartridge(tr, self.game.title, self.game.exists ? 1.0 : 0.4);
    }
    if (self.hover && self.game.exists) {
        [HEX(0x000000, 0.28) setFill]; NSRectFillUsingOperation(tr, NSCompositingOperationSourceOver);
        NSRect circ = NSMakeRect(NSMidX(tr) - 24, NSMidY(tr) - 24, 48, 48);
        [HEX(T::green) setFill]; [[NSBezierPath bezierPathWithOvalInRect:circ] fill];
        NSBezierPath *tri = [NSBezierPath bezierPath];
        [tri moveToPoint:NSMakePoint(NSMidX(circ) - 6, NSMidY(circ) - 10)];
        [tri lineToPoint:NSMakePoint(NSMidX(circ) + 11, NSMidY(circ))];
        [tri lineToPoint:NSMakePoint(NSMidX(circ) - 6, NSMidY(circ) + 10)];
        [tri closePath];
        [HEX(0xFFFFFF) setFill]; [tri fill];
    }
    [NSGraphicsContext restoreGraphicsState];

    CGFloat ty = NSMaxY(tr) + 10;
    DrawText(self.game.title, NSMakeRect(12, ty, b.size.width - 22, 18), F(13.5, NSFontWeightSemibold), HEX(T::text));
    NSString *sub = self.game.exists ? RelativeDate(self.game.lastPlayed) : @"File not found";
    DrawText(sub, NSMakeRect(12, ty + 20, b.size.width - 22, 16), F(11.5),
             self.game.exists ? HEX(T::text2) : HEX(T::danger));

    if (self.selected) {
        NSBezierPath *ring = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(b, 1, 1) xRadius:11 yRadius:11];
        ring.lineWidth = 2; [HEX(T::green) setStroke]; [ring stroke];
    }
}
@end

// ─────────────────────────────────────────────────────────────────────────────
#pragma mark - Grid

@interface SCGridView : NSView
@property (weak) id<SCHost> host;
@property (strong) NSArray<SCGame *> *games;
@property (strong) NSMutableArray<SCCardView *> *cards;
@property (copy) NSString *heading;
@property NSInteger columns;
@property (nonatomic, weak) SCGame *selected;
- (void)reload;
- (void)relayoutForWidth:(CGFloat)w minHeight:(CGFloat)h;
@end

@implementation SCGridView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)reload {
    for (NSView *v in self.cards) [v removeFromSuperview];
    self.cards = [NSMutableArray array];
    for (SCGame *g in self.games) {
        SCCardView *c = [[SCCardView alloc] initWithFrame:NSZeroRect];
        c.game = g; c.host = self.host; c.selected = (g == self.selected);
        [self addSubview:c];
        [self.cards addObject:c];
    }
    self.needsDisplay = YES;
}
- (void)setSelected:(SCGame *)selected {
    _selected = selected;
    for (SCCardView *c in self.cards) { c.selected = (c.game == selected); c.needsDisplay = YES; }
}
- (void)relayoutForWidth:(CGFloat)W minHeight:(CGFloat)minH {
    const CGFloat pad = 28, gap = 18, top = 78, minCard = 190;
    NSInteger cols = MAX(1, (NSInteger)floor((W - 2 * pad + gap) / (minCard + gap)));
    CGFloat cw = floor((W - 2 * pad - (cols - 1) * gap) / cols);
    CGFloat ch = round((cw - 16) * 0.75) + 8 + 56;
    self.columns = cols;
    for (NSUInteger i = 0; i < self.cards.count; i++) {
        NSInteger r = i / cols, c = i % cols;
        self.cards[i].frame = NSMakeRect(pad + c * (cw + gap), top + r * (ch + gap), cw, ch);
    }
    NSInteger rows = (self.cards.count + cols - 1) / cols;
    CGFloat H = top + rows * (ch + gap) + pad;
    [self setFrameSize:NSMakeSize(W, MAX(H, minH))];
    self.needsDisplay = YES;
}
- (void)drawRect:(NSRect)dirty {
    [HEX(T::bg) setFill]; NSRectFill(dirty);
    DrawText(self.heading ?: @"", NSMakeRect(28, 26, 400, 30), F(24, NSFontWeightBold), HEX(T::text));
    NSSize hs = TextSize(self.heading ?: @"", F(24, NSFontWeightBold));
    NSString *count = [NSString stringWithFormat:@"%lu %@", (unsigned long)self.games.count,
                       self.games.count == 1 ? @"game" : @"games"];
    DrawText(count, NSMakeRect(28 + hs.width + 12, 37, 200, 16), F(12, NSFontWeightMedium), HEX(T::text2));
    if (self.games.count == 0) {
        DrawText(@"Nothing here yet — play a game and it will show up here.",
                 NSMakeRect(28, 90, self.bounds.size.width - 56, 20), F(13), HEX(T::text2));
    }
}
- (void)mouseDown:(NSEvent *)e { [self.window makeFirstResponder:self]; [self.host selectGame:nil]; }
- (void)keyDown:(NSEvent *)e {
    NSInteger idx = self.selected ? (NSInteger)[self.games indexOfObject:self.selected] : -1;
    NSInteger n = self.games.count;
    if (n == 0) { [super keyDown:e]; return; }
    switch (e.keyCode) {
        case 123: idx = MAX(0, idx - 1); break;
        case 124: idx = MIN(n - 1, idx + 1); break;
        case 126: idx = idx < 0 ? 0 : MAX(0, idx - self.columns); break;
        case 125: idx = idx < 0 ? 0 : MIN(n - 1, idx + self.columns); break;
        case 36: case 76: if (self.selected) [self.host playGame:self.selected]; return;
        default: [super keyDown:e]; return;
    }
    [self.host selectGame:self.games[idx]];
    for (SCCardView *c in self.cards) if (c.game == self.games[idx]) [self scrollRectToVisible:NSInsetRect(c.frame, 0, -20)];
}
@end

// ─────────────────────────────────────────────────────────────────────────────
#pragma mark - Footer

@interface SCFooterView : NSView
@property (strong) SCButton *playButton;
@property (strong) SCButton *finderButton;
@property (nonatomic, strong) SCGame *game;
@end

@implementation SCFooterView
- (BOOL)isFlipped { return YES; }
- (void)layout {
    [super layout];
    CGFloat W = self.bounds.size.width, H = self.bounds.size.height;
    self.playButton.frame = NSMakeRect(W - 120 - 20, (H - 34) / 2, 120, 34);
    NSSize fs = self.finderButton.intrinsicContentSize;
    self.finderButton.frame = NSMakeRect(NSMinX(self.playButton.frame) - fs.width - 10, (H - 34) / 2, fs.width, 34);
}
- (void)setGame:(SCGame *)game {
    _game = game;
    self.playButton.enabled = game != nil && game.exists;
    self.finderButton.enabled = game != nil && game.exists;
    self.needsDisplay = YES; self.playButton.needsDisplay = YES; self.finderButton.needsDisplay = YES;
}
- (void)drawRect:(NSRect)dirty {
    [HEX(T::panel) setFill]; NSRectFill(self.bounds);
    [HEX(T::border) setFill]; NSRectFill(NSMakeRect(0, 0, self.bounds.size.width, 1));
    CGFloat maxW = NSMinX(self.finderButton.frame) - 40;
    if (self.game) {
        DrawText(self.game.title, NSMakeRect(24, 11, maxW, 18), F(13.5, NSFontWeightSemibold), HEX(T::text));
        DrawText([self.game.path stringByAbbreviatingWithTildeInPath], NSMakeRect(24, 30, maxW, 15), F(11), HEX(T::text2));
    } else {
        DrawText(@"Select a game to play", NSMakeRect(24, 19, maxW, 18), F(13), HEX(T::text2));
    }
}
@end

// ─────────────────────────────────────────────────────────────────────────────
#pragma mark - Empty state

@interface SCEmptyView : NSView
@property (strong) SCButton *addButton;
@end

@implementation SCEmptyView
- (BOOL)isFlipped { return YES; }
- (void)layout {
    [super layout];
    NSSize s = self.addButton.intrinsicContentSize;
    CGFloat cy = self.bounds.size.height / 2;
    self.addButton.frame = NSMakeRect(NSMidX(self.bounds) - (s.width + 20) / 2, cy + 92, s.width + 20, 38);
}
- (void)drawRect:(NSRect)dirty {
    [HEX(T::bg) setFill]; NSRectFill(self.bounds);
    CGFloat cy = self.bounds.size.height / 2;
    NSRect art = NSMakeRect(NSMidX(self.bounds) - 90, cy - 190, 180, 150);
    DrawCartridge(art, @"Super A'Can");
    DrawText(@"Your library is empty", NSMakeRect(0, cy - 20, self.bounds.size.width, 30),
             F(22, NSFontWeightBold), HEX(T::text), NSTextAlignmentCenter);
    DrawText(@"Add your Super A'Can ROM files (.bin) to get started.", NSMakeRect(0, cy + 18, self.bounds.size.width, 20),
             F(13.5), HEX(T::text2), NSTextAlignmentCenter);
    DrawText(@"You can also drag ROMs or whole folders onto this window.", NSMakeRect(0, cy + 40, self.bounds.size.width, 20),
             F(13.5), HEX(T::text2), NSTextAlignmentCenter);
}
@end

// ─────────────────────────────────────────────────────────────────────────────
#pragma mark - Drop overlay

@interface SCDropOverlay : NSView @end
@implementation SCDropOverlay
- (BOOL)isFlipped { return YES; }
- (NSView *)hitTest:(NSPoint)p { return nil; }
- (void)drawRect:(NSRect)dirty {
    [HEX(T::bg, 0.96) setFill]; NSRectFillUsingOperation(self.bounds, NSCompositingOperationSourceOver);
    NSRect r = NSInsetRect(self.bounds, 24, 24);
    NSBezierPath *p = [NSBezierPath bezierPathWithRoundedRect:r xRadius:18 yRadius:18];
    CGFloat dash[] = {10, 6};
    [p setLineDash:dash count:2 phase:0]; p.lineWidth = 3;
    [HEX(T::green) setStroke]; [p stroke];
    [HEX(T::green, 0.06) setFill]; [p fill];
    DrawText(@"Drop to add to your library", NSMakeRect(0, NSMidY(r) - 16, self.bounds.size.width, 32),
             F(22, NSFontWeightBold), HEX(T::greenHi), NSTextAlignmentCenter);
}
@end

// ─────────────────────────────────────────────────────────────────────────────
#pragma mark - Toast

@interface SCToastView : NSView
@property (copy) NSString *message;
@end
@implementation SCToastView
- (BOOL)isFlipped { return YES; }
- (NSView *)hitTest:(NSPoint)p { return nil; }
- (void)drawRect:(NSRect)dirty {
    NSBezierPath *p = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds, 0.5, 0.5)
                                                      xRadius:self.bounds.size.height / 2 yRadius:self.bounds.size.height / 2];
    [HEX(T::panel, 0.94) setFill]; [p fill];
    [HEX(T::border) setStroke]; [p stroke];
    [HEX(T::green) setFill];
    [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(16, self.bounds.size.height / 2 - 4, 8, 8)] fill];
    DrawText(self.message ?: @"", NSMakeRect(32, (self.bounds.size.height - 17) / 2, self.bounds.size.width - 46, 18),
             F(13, NSFontWeightSemibold), HEX(T::text));
}
@end

// ─────────────────────────────────────────────────────────────────────────────
#pragma mark - Pause overlay

@interface SCPauseView : NSView
@property (copy) NSString *gameTitle;
@property (strong) NSArray<SCButton *> *buttons;
@property (strong) NSSegmentedControl *slots;
@property (strong) NSString *slotInfo;
@end

@implementation SCPauseView
- (BOOL)isFlipped { return YES; }
- (NSRect)panelRect {
    CGFloat w = 320, h = 96 + self.buttons.count * 46 + 30 + 24 + 28;
    return NSMakeRect(round(NSMidX(self.bounds) - w / 2), round(NSMidY(self.bounds) - h / 2), w, h);
}
- (void)layout {
    [super layout];
    NSRect p = [self panelRect];
    CGFloat y = p.origin.y + 96;
    for (SCButton *b in self.buttons) { b.frame = NSMakeRect(p.origin.x + 30, y, p.size.width - 60, 36); y += 46; }
    self.slots.frame = NSMakeRect(p.origin.x + 30, y + 30, p.size.width - 60, 24);
}
- (void)mouseDown:(NSEvent *)e {} // swallow clicks on the backdrop
- (void)drawRect:(NSRect)dirty {
    [HEX(0x000000, 0.55) setFill]; NSRectFillUsingOperation(self.bounds, NSCompositingOperationSourceOver);
    NSRect p = [self panelRect];
    NSBezierPath *path = [NSBezierPath bezierPathWithRoundedRect:p xRadius:16 yRadius:16];
    [HEX(T::panel, 0.98) setFill]; [path fill];
    [HEX(T::border) setStroke]; path.lineWidth = 1; [path stroke];
    // Console-style accent stripe
    [NSGraphicsContext saveGraphicsState];
    [path addClip];
    [HEX(T::green) setFill]; NSRectFill(NSMakeRect(p.origin.x, p.origin.y, p.size.width, 4));
    [NSGraphicsContext restoreGraphicsState];

    DrawText(@"PAUSED", NSMakeRect(p.origin.x, p.origin.y + 28, p.size.width, 14), F(11, NSFontWeightBold),
             HEX(T::green), NSTextAlignmentCenter, 2.4);
    DrawText(self.gameTitle ?: @"", NSMakeRect(p.origin.x + 20, p.origin.y + 48, p.size.width - 40, 24),
             F(18, NSFontWeightBold), HEX(T::text), NSTextAlignmentCenter);

    CGFloat y = NSMinY(self.slots.frame) - 22;
    DrawText(@"STATE SLOT", NSMakeRect(p.origin.x + 30, y, 120, 14), F(10, NSFontWeightBold), HEX(T::text2),
             NSTextAlignmentLeft, 1.4);
    DrawText(self.slotInfo ?: @"", NSMakeRect(p.origin.x + 130, y, p.size.width - 160, 14), F(11), HEX(T::text2),
             NSTextAlignmentRight);
}
@end

// ─────────────────────────────────────────────────────────────────────────────
#pragma mark - Game view

@interface SCGameView : NSView
@property (weak) id<SCHost> host;
@property (nonatomic) CGImageRef snapshotImage; // debug snapshots only
// Live frames. Always a 4:3 box centred in the view: a 256 px (H256) frame is stretched to the
// same picture as a 320 px one, as on a TV.
@property (strong) CALayer *picture;
@end

@implementation SCGameView
- (instancetype)initWithFrame:(NSRect)r {
    if ((self = [super initWithFrame:r])) {
        self.wantsLayer = YES;
        self.layer.backgroundColor = HEX(T::gameBg).CGColor;
        self.layer.contentsGravity = kCAGravityResizeAspect;
        self.layer.magnificationFilter = kCAFilterNearest;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawNever;
        _picture = [CALayer layer];
        _picture.contentsGravity = kCAGravityResize;
        _picture.magnificationFilter = kCAFilterNearest;
        _picture.actions = @{ @"contents": [NSNull null], @"bounds": [NSNull null],
                              @"position": [NSNull null] };
        [self.layer addSublayer:_picture];
    }
    return self;
}
- (void)layout {
    [super layout];
    NSRect g = self.bounds;
    CGFloat s = MIN(g.size.width / 320.0, g.size.height / 240.0);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    _picture.frame = NSMakeRect(NSMidX(g) - 160 * s, NSMidY(g) - 120 * s, 320 * s, 240 * s);
    [CATransaction commit];
}
- (void)setFrameSize:(NSSize)size { [super setFrameSize:size]; self.needsLayout = YES; }
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)wantsUpdateLayer { return YES; }
- (void)updateLayer {}
- (void)setSnapshotImage:(CGImageRef)img {
    if (_snapshotImage) CGImageRelease(_snapshotImage);
    _snapshotImage = img ? CGImageRetain(img) : nullptr;
}
- (void)drawRect:(NSRect)dirty {
    // Only used by cacheDisplayInRect for debug snapshots; live frames go through layer.contents.
    [HEX(T::gameBg) setFill]; NSRectFill(self.bounds);
    if (!_snapshotImage) return;
    NSRect g = self.bounds;
    CGFloat s = MIN(g.size.width / 320.0, g.size.height / 240.0);
    NSRect dst = NSMakeRect(NSMidX(g) - 160 * s, NSMidY(g) - 120 * s, 320 * s, 240 * s);
    CGContextRef c = NSGraphicsContext.currentContext.CGContext;
    CGContextSaveGState(c);
    CGContextTranslateCTM(c, 0, NSMaxY(dst) + NSMinY(dst));
    CGContextScaleCTM(c, 1, -1);
    CGContextSetInterpolationQuality(c, kCGInterpolationNone);
    CGContextDrawImage(c, dst, _snapshotImage);
    CGContextRestoreGState(c);
}
- (void)keyDown:(NSEvent *)e { [self.host gameKey:e down:YES]; }
- (void)keyUp:(NSEvent *)e { [self.host gameKey:e down:NO]; }
- (void)mouseDown:(NSEvent *)e { [self.window makeFirstResponder:self]; }
@end

// ─────────────────────────────────────────────────────────────────────────────
#pragma mark - Root view

@interface SCRootView : NSView
@property (weak) id<SCHost> host;
@property (strong) SCHeaderView *header;
@property (strong) SCSidebarView *sidebar;
@property (strong) NSScrollView *scroll;
@property (strong) SCGridView *grid;
@property (strong) SCFooterView *footer;
@property (strong) SCEmptyView *empty;
@property (strong) SCGameView *game;
@property (strong) SCPauseView *pause;
@property (strong) SCToastView *toast;
@property (strong) SCDropOverlay *drop;
@property BOOL gameMode, libraryEmpty;
@end

@implementation SCRootView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)layout {
    [super layout];
    NSRect b = self.bounds;
    CGFloat W = b.size.width, H = b.size.height;
    BOOL g = self.gameMode;
    self.header.hidden = self.sidebar.hidden = self.scroll.hidden = self.footer.hidden = g;
    self.empty.hidden = g || !self.libraryEmpty;
    self.footer.hidden = g || self.libraryEmpty;
    self.scroll.hidden = g || self.libraryEmpty;
    self.game.hidden = !g;

    if (g) {
        CGFloat top = H - self.window.contentLayoutRect.size.height; // title bar height (0 in full screen)
        self.game.frame = NSMakeRect(0, top, W, H - top);
        self.pause.frame = self.game.frame;
    } else {
        const CGFloat hh = 64, sw = 220, fh = 56;
        self.header.frame = NSMakeRect(0, 0, W, hh);
        self.sidebar.frame = NSMakeRect(0, hh, sw, H - hh);
        self.footer.frame = NSMakeRect(sw, H - fh, W - sw, fh);
        self.scroll.frame = NSMakeRect(sw, hh, W - sw, H - hh - fh);
        self.empty.frame = NSMakeRect(sw, hh, W - sw, H - hh);
        self.drop.frame = NSMakeRect(sw, hh, W - sw, H - hh);
        [self.grid relayoutForWidth:self.scroll.contentSize.width minHeight:self.scroll.contentSize.height];
    }
    NSSize ts = TextSize(self.toast.message ?: @"", F(13, NSFontWeightSemibold));
    CGFloat tw = ceil(ts.width) + 50;
    CGFloat ty = g ? NSMinY(self.game.frame) + 20 : 80;
    self.toast.frame = NSMakeRect(round((W - tw) / 2), ty, tw, 36);
}
- (void)drawRect:(NSRect)dirty { [HEX(self.gameMode ? T::gameBg : T::bg) setFill]; NSRectFill(dirty); }

// Drag & drop
- (NSArray<NSURL *> *)urlsFrom:(id<NSDraggingInfo>)info {
    return [info.draggingPasteboard readObjectsForClasses:@[NSURL.class]
                                                  options:@{ NSPasteboardURLReadingFileURLsOnlyKey: @YES }];
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)info {
    if ([self urlsFrom:info].count == 0) return NSDragOperationNone;
    if (!self.gameMode) self.drop.hidden = NO;
    return NSDragOperationCopy;
}
- (void)draggingExited:(id<NSDraggingInfo>)info { self.drop.hidden = YES; }
- (BOOL)performDragOperation:(id<NSDraggingInfo>)info {
    self.drop.hidden = YES;
    NSArray *urls = [self urlsFrom:info];
    // A single ROM dropped while playing switches to it; otherwise add to library.
    BOOL play = self.gameMode && urls.count == 1;
    [self.host addURLs:urls play:play];
    return YES;
}
@end

// ─────────────────────────────────────────────────────────────────────────────
#pragma mark - App delegate / controller

static const uint16_t kAllButtons[] = { BTN_R, BTN_L, BTN_Y, BTN_X, BTN_RIGHT, BTN_LEFT, BTN_DOWN, BTN_UP,
                                        BTN_SELECT, BTN_START, BTN_B, BTN_A };

@interface SCApp : NSObject <NSApplicationDelegate, NSWindowDelegate, SCHost, NSSearchFieldDelegate, NSMenuItemValidation>
- (void)queueURLs:(NSArray<NSURL *> *)urls;
@end

@implementation SCApp {
    NSWindow *_window;
    SCRootView *_root;
    NSMutableArray<SCGame *> *_games;
    NSInteger _filter;
    NSString *_search;
    SCGame *_selected;

    std::unique_ptr<SuperAcan> _emu;
    AudioOut _audio;
    SCGame *_current;
    BOOL _paused, _fastForward, _smooth;
    int _slot;
    uint16_t _keys;
    NSTimer *_timer;
    CFTimeInterval _last;
    double _acc;
    int _fpsFrames; CFTimeInterval _fpsStart; double _fps;
    NSTimer *_toastTimer;
    NSMutableArray<NSURL *> *_pending;
    BOOL _ready;
}

// ── Lifecycle ──────────────────────────────────────────────────────────────
- (void)applicationWillFinishLaunching:(NSNotification *)n {
    [self buildMenu];
}

- (void)applicationDidFinishLaunching:(NSNotification *)n {
    _slot = 1;
    _smooth = [NSUserDefaults.standardUserDefaults boolForKey:@"smoothScaling"];
    [self loadLibrary];

    _window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1120, 740)
                                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                    NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable |
                                                    NSWindowStyleMaskFullSizeContentView
                                            backing:NSBackingStoreBuffered defer:NO];
    _window.titlebarAppearsTransparent = YES;
    _window.titleVisibility = NSWindowTitleHidden;
    _window.title = @"Super A'Can Emulator";
    _window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    _window.backgroundColor = HEX(T::bg);
    _window.minSize = NSMakeSize(860, 580);
    _window.delegate = self;
    _window.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
    [_window center];
    [_window setFrameAutosaveName:@"MainWindow"];

    [self buildViews];
    [self refreshLibrary];
    _window.initialFirstResponder = _root.grid;
    [_window makeKeyAndOrderFront:nil];
    [_window makeFirstResponder:_games.count ? (NSResponder *)_root.grid : (NSResponder *)_root];

    _timer = [NSTimer timerWithTimeInterval:1.0 / 240.0 target:self selector:@selector(tick) userInfo:nil repeats:YES];
    [NSRunLoop.mainRunLoop addTimer:_timer forMode:NSRunLoopCommonModes];
    _last = CACurrentMediaTime();
    _ready = YES;
    [self runDebugScript];
    if (_pending.count) { NSArray *u = _pending; _pending = nil; [self addURLs:u play:u.count == 1]; }
}

- (void)queueURLs:(NSArray<NSURL *> *)urls { [self application:NSApp openURLs:urls]; }

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)a { return YES; }
- (void)applicationWillTerminate:(NSNotification *)n { if (_emu) [self closeGame:nil]; _audio.shutdown(); }
- (BOOL)applicationSupportsSecureRestorableState:(NSApplication *)a { return YES; }

- (void)application:(NSApplication *)app openURLs:(NSArray<NSURL *> *)urls {
    // Finder "Open With", dock drops, or `open -a`
    if (!_ready) { if (!_pending) _pending = [NSMutableArray array]; [_pending addObjectsFromArray:urls]; return; }
    [self addURLs:urls play:urls.count == 1];
}

- (void)windowDidResignKey:(NSNotification *)n { _keys = 0; _fastForward = NO; }
- (void)windowDidEnterFullScreen:(NSNotification *)n { _root.needsLayout = YES; }
- (void)windowDidExitFullScreen:(NSNotification *)n { _root.needsLayout = YES; }

// ── Views ──────────────────────────────────────────────────────────────────
- (void)buildViews {
    _root = [[SCRootView alloc] initWithFrame:_window.contentView.bounds];
    _root.host = self;
    [_root registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    _window.contentView = _root;

    // Library
    _root.grid = [[SCGridView alloc] initWithFrame:NSZeroRect];
    _root.grid.host = self;
    _root.scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    _root.scroll.documentView = _root.grid;
    _root.scroll.hasVerticalScroller = YES;
    _root.scroll.autohidesScrollers = YES;
    _root.scroll.scrollerStyle = NSScrollerStyleOverlay;
    _root.scroll.drawsBackground = YES;
    _root.scroll.backgroundColor = HEX(T::bg);
    _root.scroll.contentView.postsFrameChangedNotifications = YES;
    [NSNotificationCenter.defaultCenter addObserverForName:NSViewFrameDidChangeNotification
                                                    object:_root.scroll.contentView queue:nil
                                                usingBlock:^(NSNotification *note) { self->_root.needsLayout = YES; }];
    [_root addSubview:_root.scroll];

    _root.header = [[SCHeaderView alloc] initWithFrame:NSZeroRect];
    _root.header.search = [[NSSearchField alloc] initWithFrame:NSZeroRect];
    _root.header.search.placeholderString = @"Search games";
    _root.header.search.delegate = self;
    _root.header.search.focusRingType = NSFocusRingTypeNone;
    _root.header.search.sendsSearchStringImmediately = YES;
    _root.header.addButton = [SCButton withTitle:@"+  Add ROMs" style:SCButtonPrimary target:self action:@selector(addROMs:)];
    [_root.header addSubview:_root.header.search];
    [_root.header addSubview:_root.header.addButton];
    [_root addSubview:_root.header];

    _root.sidebar = [[SCSidebarView alloc] initWithFrame:NSZeroRect];
    _root.sidebar.host = self;
    [_root addSubview:_root.sidebar];

    _root.footer = [[SCFooterView alloc] initWithFrame:NSZeroRect];
    _root.footer.playButton = [SCButton withTitle:@"▶  Play" style:SCButtonPrimary target:self action:@selector(playSelected:)];
    _root.footer.finderButton = [SCButton withTitle:@"Show in Finder" style:SCButtonSecondary target:self action:@selector(showSelectedInFinder:)];
    [_root.footer addSubview:_root.footer.playButton];
    [_root.footer addSubview:_root.footer.finderButton];
    _root.footer.game = nil;
    [_root addSubview:_root.footer];

    _root.empty = [[SCEmptyView alloc] initWithFrame:NSZeroRect];
    _root.empty.addButton = [SCButton withTitle:@"Add ROMs…" style:SCButtonPrimary target:self action:@selector(addROMs:)];
    [_root.empty addSubview:_root.empty.addButton];
    [_root addSubview:_root.empty];

    // Game
    _root.game = [[SCGameView alloc] initWithFrame:NSZeroRect];
    _root.game.host = self;
    _root.game.layer.minificationFilter = kCAFilterTrilinear;
    [_root addSubview:_root.game];

    _root.pause = [[SCPauseView alloc] initWithFrame:NSZeroRect];
    NSMutableArray *pb = [NSMutableArray array];
    [pb addObject:[SCButton withTitle:@"Resume" style:SCButtonPrimary target:self action:@selector(togglePause:)]];
    [pb addObject:[SCButton withTitle:@"Save State" style:SCButtonSecondary target:self action:@selector(saveState:)]];
    [pb addObject:[SCButton withTitle:@"Load State" style:SCButtonSecondary target:self action:@selector(loadState:)]];
    [pb addObject:[SCButton withTitle:@"Reset" style:SCButtonSecondary target:self action:@selector(resetGame:)]];
    [pb addObject:[SCButton withTitle:@"Quit to Library" style:SCButtonGhost target:self action:@selector(closeGame:)]];
    _root.pause.buttons = pb;
    for (SCButton *b in pb) [_root.pause addSubview:b];
    _root.pause.slots = [NSSegmentedControl segmentedControlWithLabels:@[@"1", @"2", @"3", @"4", @"5"]
                                                          trackingMode:NSSegmentSwitchTrackingSelectOne
                                                                target:self action:@selector(slotSegmentChanged:)];
    _root.pause.slots.segmentDistribution = NSSegmentDistributionFillEqually;
    _root.pause.slots.selectedSegmentBezelColor = HEX(T::green);
    _root.pause.slots.refusesFirstResponder = YES;
    [_root.pause addSubview:_root.pause.slots];
    _root.pause.hidden = YES;
    [_root.game addSubview:_root.pause];
    _root.pause.frame = _root.game.bounds;
    _root.pause.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    _root.drop = [[SCDropOverlay alloc] initWithFrame:NSZeroRect];
    _root.drop.hidden = YES;
    [_root addSubview:_root.drop];

    _root.toast = [[SCToastView alloc] initWithFrame:NSZeroRect];
    _root.toast.wantsLayer = YES;
    _root.toast.alphaValue = 0;
    [_root addSubview:_root.toast];
}

// ── Menu ───────────────────────────────────────────────────────────────────
- (NSMenuItem *)item:(NSString *)t action:(SEL)a key:(NSString *)k mods:(NSEventModifierFlags)m {
    NSMenuItem *i = [[NSMenuItem alloc] initWithTitle:t action:a keyEquivalent:k ?: @""];
    i.keyEquivalentModifierMask = m;
    return i;
}
- (void)buildMenu {
    NSMenu *bar = [NSMenu new];
    const NSEventModifierFlags C = NSEventModifierFlagCommand, S = NSEventModifierFlagShift;

    NSMenu *app = [[NSMenu alloc] initWithTitle:@"Super A'Can"];
    [app addItem:[self item:@"About Super A'Can Emulator" action:@selector(about:) key:nil mods:0]];
    [app addItem:NSMenuItem.separatorItem];
    [app addItem:[self item:@"Hide Super A'Can Emulator" action:@selector(hide:) key:@"h" mods:C]];
    [app addItem:[self item:@"Hide Others" action:@selector(hideOtherApplications:) key:@"h" mods:C | NSEventModifierFlagOption]];
    [app addItem:[self item:@"Show All" action:@selector(unhideAllApplications:) key:nil mods:0]];
    [app addItem:NSMenuItem.separatorItem];
    [app addItem:[self item:@"Quit Super A'Can Emulator" action:@selector(terminate:) key:@"q" mods:C]];

    NSMenu *file = [[NSMenu alloc] initWithTitle:@"File"];
    [file addItem:[self item:@"Open ROM…" action:@selector(openROM:) key:@"o" mods:C]];
    [file addItem:[self item:@"Add ROMs to Library…" action:@selector(addROMs:) key:@"o" mods:C | S]];
    [file addItem:NSMenuItem.separatorItem];
    [file addItem:[self item:@"Close Game" action:@selector(closeGame:) key:@"w" mods:C]];

    NSMenu *edit = [[NSMenu alloc] initWithTitle:@"Edit"];
    [edit addItem:[self item:@"Cut" action:@selector(cut:) key:@"x" mods:C]];
    [edit addItem:[self item:@"Copy" action:@selector(copy:) key:@"c" mods:C]];
    [edit addItem:[self item:@"Paste" action:@selector(paste:) key:@"v" mods:C]];
    [edit addItem:[self item:@"Select All" action:@selector(selectAll:) key:@"a" mods:C]];
    [edit addItem:NSMenuItem.separatorItem];
    [edit addItem:[self item:@"Find" action:@selector(focusSearch:) key:@"f" mods:C]];

    NSMenu *emu = [[NSMenu alloc] initWithTitle:@"Emulation"];
    [emu addItem:[self item:@"Pause" action:@selector(togglePause:) key:@"p" mods:C]];
    [emu addItem:[self item:@"Reset" action:@selector(resetGame:) key:@"r" mods:C]];
    [emu addItem:NSMenuItem.separatorItem];
    [emu addItem:[self item:@"Save State" action:@selector(saveState:) key:@"s" mods:C]];
    [emu addItem:[self item:@"Load State" action:@selector(loadState:) key:@"l" mods:C]];
    NSMenuItem *slotItem = [self item:@"State Slot" action:nil key:nil mods:0];
    NSMenu *slots = [[NSMenu alloc] initWithTitle:@"State Slot"];
    for (int i = 1; i <= 5; i++) {
        NSMenuItem *s = [self item:[NSString stringWithFormat:@"Slot %d", i] action:@selector(selectSlot:)
                               key:[NSString stringWithFormat:@"%d", i] mods:C];
        s.tag = i; [slots addItem:s];
    }
    slotItem.submenu = slots;
    [emu addItem:slotItem];
    [emu addItem:NSMenuItem.separatorItem];
    [emu addItem:[self item:@"Mute Audio" action:@selector(toggleMute:) key:@"m" mods:C | S]];
    NSMenuItem *styleItem = [self item:@"Sound Style" action:nil key:nil mods:0];
    NSMenu *styles = [[NSMenu alloc] initWithTitle:@"Sound Style"];
    NSMenuItem *acc = [self item:@"Accurate (hardware envelopes)" action:@selector(setSoundStyle:) key:nil mods:0];
    acc.tag = 0;
    acc.toolTip = @"Notes fade in and out as the console's sound chip shapes them (based on the MiSTer FPGA core).";
    NSMenuItem *sus = [self item:@"Sustained (fuller, like MAME)" action:@selector(setSoundStyle:) key:nil mods:0];
    sus.tag = 1;
    sus.toolTip = @"Notes hold full volume until released, as in MAME. Louder, with more bass; less accurate.";
    [styles addItem:acc];
    [styles addItem:sus];
    styleItem.submenu = styles;
    [emu addItem:styleItem];

    NSMenu *view = [[NSMenu alloc] initWithTitle:@"View"];
    [view addItem:[self item:@"Smooth Scaling" action:@selector(toggleSmooth:) key:nil mods:0]];
    NSMenuItem *sizeItem = [self item:@"Window Size" action:nil key:nil mods:0];
    NSMenu *sizes = [[NSMenu alloc] initWithTitle:@"Window Size"];
    for (int i = 2; i <= 4; i++) {
        NSMenuItem *s = [self item:[NSString stringWithFormat:@"%d×", i] action:@selector(setWindowScale:)
                               key:[NSString stringWithFormat:@"%d", i] mods:C | NSEventModifierFlagOption];
        s.tag = i; [sizes addItem:s];
    }
    sizeItem.submenu = sizes;
    [view addItem:sizeItem];
    [view addItem:NSMenuItem.separatorItem];
    [view addItem:[self item:@"Enter Full Screen" action:@selector(toggleFullScreen:) key:@"f" mods:C | NSEventModifierFlagControl]];

    NSMenu *win = [[NSMenu alloc] initWithTitle:@"Window"];
    [win addItem:[self item:@"Minimize" action:@selector(performMiniaturize:) key:@"m" mods:C]];
    [win addItem:[self item:@"Zoom" action:@selector(performZoom:) key:nil mods:0]];

    for (NSMenu *m in @[app, file, edit, emu, view, win]) {
        NSMenuItem *top = [NSMenuItem new];
        top.submenu = m;
        [bar addItem:top];
    }
    NSApp.mainMenu = bar;
    NSApp.windowsMenu = win;
}

- (BOOL)validateMenuItem:(NSMenuItem *)item {
    SEL a = item.action;
    BOOL running = _emu != nullptr;
    if (a == @selector(togglePause:)) { item.title = _paused ? @"Resume" : @"Pause"; return running; }
    if (a == @selector(resetGame:) || a == @selector(saveState:) || a == @selector(closeGame:)) return running;
    if (a == @selector(loadState:)) return running && [self slotExists:_slot];
    if (a == @selector(selectSlot:)) { item.state = item.tag == _slot ? NSControlStateValueOn : NSControlStateValueOff; return YES; }
    if (a == @selector(setSoundStyle:)) {
        BOOL sustained = [NSUserDefaults.standardUserDefaults boolForKey:@"soundSustained"];
        item.state = (item.tag == (sustained ? 1 : 0)) ? NSControlStateValueOn : NSControlStateValueOff;
        return YES;
    }
    if (a == @selector(toggleMute:)) { item.state = _audio.isMuted() ? NSControlStateValueOn : NSControlStateValueOff; return YES; }
    if (a == @selector(toggleSmooth:)) { item.state = _smooth ? NSControlStateValueOn : NSControlStateValueOff; return YES; }
    if (a == @selector(focusSearch:)) return !running;
    return YES;
}

// ── Library ────────────────────────────────────────────────────────────────
- (void)loadLibrary {
    _games = [NSMutableArray array];
    for (NSDictionary *d in [NSUserDefaults.standardUserDefaults arrayForKey:@"library"] ?: @[]) {
        if (d[@"path"]) [_games addObject:[SCGame gameWithDict:d]];
    }
}
- (void)saveLibrary {
    NSMutableArray *arr = [NSMutableArray array];
    for (SCGame *g in _games) [arr addObject:[g dict]];
    [NSUserDefaults.standardUserDefaults setObject:arr forKey:@"library"];
}
- (NSArray<SCGame *> *)visibleGames {
    NSArray *list = _games;
    if (_filter == 1) {
        list = [[_games filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"lastPlayed != nil"]]
                sortedArrayUsingComparator:^NSComparisonResult(SCGame *a, SCGame *b) { return [b.lastPlayed compare:a.lastPlayed]; }];
    } else {
        list = [_games sortedArrayUsingComparator:^NSComparisonResult(SCGame *a, SCGame *b) {
            return [a.title localizedStandardCompare:b.title];
        }];
    }
    if (_search.length) {
        list = [list filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"title CONTAINS[cd] %@", _search]];
    }
    return list;
}
- (void)refreshLibrary {
    _root.libraryEmpty = _games.count == 0;
    _root.grid.games = [self visibleGames];
    _root.grid.heading = _search.length ? @"Search Results" : (_filter == 1 ? @"Recently Played" : @"All Games");
    if (_selected && ![_root.grid.games containsObject:_selected]) _selected = nil;
    _root.grid.selected = _selected;
    [_root.grid reload];
    _root.footer.game = _selected;
    _root.sidebar.needsDisplay = YES;
    _root.needsLayout = YES;
}
- (NSInteger)filter { return _filter; }
- (void)setFilter:(NSInteger)f { _filter = f; [self refreshLibrary]; }
- (NSInteger)countForFilter:(NSInteger)f {
    if (f == 0) return _games.count;
    NSInteger n = 0; for (SCGame *g in _games) if (g.lastPlayed) n++;
    return n;
}
- (void)controlTextDidChange:(NSNotification *)n { _search = _root.header.search.stringValue; [self refreshLibrary]; }
- (void)focusSearch:(id)s { [_window makeFirstResponder:_root.header.search]; }

- (void)selectGame:(SCGame *)g {
    _selected = g;
    _root.grid.selected = g;
    _root.footer.game = g;
    if (_window.firstResponder != _root.grid) [_window makeFirstResponder:_root.grid];
}

static BOOL IsDataROM(NSString *name) {
    NSString *l = name.lowercaseString;
    return [l containsString:@"_dat"] || [l containsString:@" dat"] || [l containsString:@"-dat"] || [l hasPrefix:@"08002"];
}

- (NSArray<NSString *> *)romsIn:(NSArray<NSURL *> *)urls {
    NSMutableArray *out = [NSMutableArray array];
    NSFileManager *fm = NSFileManager.defaultManager;
    void (^consider)(NSURL *) = ^(NSURL *u) {
        NSString *ext = u.pathExtension.lowercaseString;
        if (![ext isEqualToString:@"bin"] || IsDataROM(u.lastPathComponent)) return;
        NSNumber *size; [u getResourceValue:&size forKey:NSURLFileSizeKey error:nil];
        if (size && size.unsignedLongLongValue < 64 * 1024) return;
        [out addObject:u.URLByStandardizingPath.path];
    };
    for (NSURL *u in urls) {
        BOOL dir = NO;
        if (![fm fileExistsAtPath:u.path isDirectory:&dir]) continue;
        if (!dir) { consider(u); continue; }
        NSDirectoryEnumerator *en = [fm enumeratorAtURL:u includingPropertiesForKeys:@[NSURLFileSizeKey]
                                                options:NSDirectoryEnumerationSkipsHiddenFiles errorHandler:nil];
        for (NSURL *f in en) consider(f);
    }
    return out;
}

- (void)addURLs:(NSArray<NSURL *> *)urls play:(BOOL)play {
    NSArray *paths = [self romsIn:urls];
    NSInteger added = 0;
    SCGame *last = nil;
    for (NSString *p in paths) {
        SCGame *existing = nil;
        for (SCGame *g in _games) if ([g.path isEqualToString:p]) existing = g;
        if (!existing) {
            existing = [SCGame new];
            existing.path = p;
            existing.title = [SCGame titleForPath:p];
            [existing loadCover];
            [_games addObject:existing];
            added++;
        }
        last = existing;
    }
    [self saveLibrary];
    if (!_emu) { _selected = last ?: _selected; [self refreshLibrary]; }
    // Only auto-play when exactly one ROM *file* was opened (never for folders).
    BOOL singleFile = urls.count == 1 && paths.count == 1 && !urls.firstObject.hasDirectoryPath;
    if (play && singleFile && last) { [self playGame:last]; return; }
    if (paths.count == 0) [self toast:@"No Super A'Can ROMs (.bin) found"];
    else if (added == 0) [self toast:@"Already in your library"];
    else [self toast:[NSString stringWithFormat:@"Added %ld %@", (long)added, added == 1 ? @"game" : @"games"]];
}

- (NSOpenPanel *)romPanel:(BOOL)dirs {
    NSOpenPanel *p = [NSOpenPanel openPanel];
    p.canChooseFiles = YES;
    p.canChooseDirectories = dirs;
    p.allowsMultipleSelection = dirs;
    UTType *bin = [UTType typeWithFilenameExtension:@"bin"];
    if (bin) p.allowedContentTypes = @[bin];
    p.message = dirs ? @"Choose Super A'Can ROMs or a folder containing them" : @"Choose a Super A'Can ROM to play";
    p.prompt = dirs ? @"Add" : @"Play";
    return p;
}
- (void)addROMs:(id)sender {
    NSOpenPanel *p = [self romPanel:YES];
    [p beginSheetModalForWindow:_window completionHandler:^(NSModalResponse r) {
        if (r == NSModalResponseOK) [self addURLs:p.URLs play:NO];
    }];
}
- (void)openROM:(id)sender {
    BOOL wasRunning = _emu && !_paused;
    if (wasRunning) [self setPaused:YES showMenu:NO];
    NSOpenPanel *p = [self romPanel:NO];
    [p beginSheetModalForWindow:_window completionHandler:^(NSModalResponse r) {
        if (r == NSModalResponseOK) [self addURLs:p.URLs play:YES];
        else if (wasRunning) [self setPaused:NO showMenu:NO];
    }];
}

- (NSMenu *)menuForGame:(SCGame *)g {
    NSMenu *m = [NSMenu new];
    NSMenuItem *play = [m addItemWithTitle:@"Play" action:@selector(menuPlay:) keyEquivalent:@""];
    play.representedObject = g; play.target = self;
    NSMenuItem *finder = [m addItemWithTitle:@"Show in Finder" action:@selector(menuFinder:) keyEquivalent:@""];
    finder.representedObject = g; finder.target = self;
    [m addItem:NSMenuItem.separatorItem];
    NSMenuItem *rm = [m addItemWithTitle:@"Remove from Library" action:@selector(menuRemove:) keyEquivalent:@""];
    rm.representedObject = g; rm.target = self;
    return m;
}
- (void)menuPlay:(NSMenuItem *)i { [self playGame:i.representedObject]; }
- (void)menuFinder:(NSMenuItem *)i { [self revealGame:i.representedObject]; }
- (void)menuRemove:(NSMenuItem *)i {
    SCGame *g = i.representedObject;
    [_games removeObject:g];
    if (_selected == g) _selected = nil;
    [self saveLibrary];
    [self refreshLibrary];
    [self toast:[NSString stringWithFormat:@"Removed “%@”", g.title]];
}
- (void)revealGame:(SCGame *)g {
    if (g.exists) [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[[NSURL fileURLWithPath:g.path]]];
}
- (void)playSelected:(id)s { if (_selected) [self playGame:_selected]; }
- (void)showSelectedInFinder:(id)s { if (_selected) [self revealGame:_selected]; }

// ── Running games ──────────────────────────────────────────────────────────
- (BOOL)inGame { return _emu != nullptr; }

// Super Dragon Force needs its companion data ROM (DAT); look next to the program ROM.
- (NSString *)dataROMFor:(SCGame *)g {
    NSString *lower = g.path.lastPathComponent.lowercaseString;
    BOOL sdf = [lower containsString:@"16007"] || [lower containsString:@"dragon-force"] ||
               [lower containsString:@"dragon force"] || [lower containsString:@"dragon_force"] ||
               [lower containsString:@"dragonforce"];
    if (!sdf) return nil;
    NSString *dir = g.path.stringByDeletingLastPathComponent;
    NSMutableArray *cands = [NSMutableArray array];
    if ([lower containsString:@"_en"]) [cands addObject:[dir stringByAppendingPathComponent:@"Super-Dragon-Force_DAT_EN.bin"]];
    for (NSString *f in [NSFileManager.defaultManager contentsOfDirectoryAtPath:dir error:nil] ?: @[]) {
        if (IsDataROM(f) && [f.pathExtension.lowercaseString isEqualToString:@"bin"])
            [cands addObject:[dir stringByAppendingPathComponent:f]];
    }
    [cands addObject:[dir stringByAppendingPathComponent:@"../Super-Dragon-Force_Super-Acan_ZH-TW/08002 (Taiwan).bin"]];
    for (NSString *c in cands) if ([NSFileManager.defaultManager fileExistsAtPath:c]) return c.stringByStandardizingPath;
    return nil;
}

- (void)playGame:(SCGame *)g {
    if (!g) return;
    if (!g.exists) {
        NSAlert *a = [NSAlert new];
        a.messageText = @"ROM file not found";
        a.informativeText = [NSString stringWithFormat:@"“%@” could not be found at:\n%@\n\nIt may have been moved or deleted.",
                             g.title, [g.path stringByAbbreviatingWithTildeInPath]];
        [a addButtonWithTitle:@"OK"];
        [a addButtonWithTitle:@"Remove from Library"];
        [a beginSheetModalForWindow:_window completionHandler:^(NSModalResponse r) {
            if (r == NSAlertSecondButtonReturn) { [self->_games removeObject:g]; [self saveLibrary]; [self refreshLibrary]; }
        }];
        return;
    }
    if (_emu) [self closeGame:nil];

    NSString *dat = [self dataROMFor:g];
    auto emu = std::make_unique<SuperAcan>();
    if (!emu->load_roms(g.path.fileSystemRepresentation, dat ? dat.fileSystemRepresentation : "")) {
        NSAlert *a = [NSAlert new];
        a.messageText = @"Couldn't load ROM";
        a.informativeText = [NSString stringWithFormat:@"“%@” isn't a valid Super A'Can ROM.", g.path.lastPathComponent];
        [a beginSheetModalForWindow:_window completionHandler:nil];
        return;
    }
    // In-game saves: the cartridge's battery RAM is kept in Saves/<game>.sav.
    {
        NSData *sav = [NSData dataWithContentsOfURL:[g batterySaveURL]];
        auto &sram = emu->get_bus().get_sram();
        if (sav.length == sram.size()) memcpy(sram.data(), sav.bytes, sram.size());
        emu->get_bus().take_sram_dirty();
    }
    emu->get_apu().set_sustained([NSUserDefaults.standardUserDefaults boolForKey:@"soundSustained"]);
    _emu = std::move(emu);
    _current = g;
    g.lastPlayed = [NSDate date];
    [self saveLibrary];

    _keys = 0; _acc = 0; _paused = NO; _fastForward = NO;
    _fpsFrames = 0; _fpsStart = CACurrentMediaTime();
    _root.gameMode = YES;
    _root.pause.hidden = YES;
    _root.game.picture.contents = nil;
    [self applyScaling];
    _root.needsLayout = YES; _root.needsDisplay = YES;
    [_window makeFirstResponder:_root.game];
    _window.title = g.title;
    if (!_audio.start()) NSLog(@"[Audio] Could not open audio output; running silently");
    else NSLog(@"[Audio] Output started at %.0f Hz", AudioOut::kRate);
    _last = CACurrentMediaTime();
    [self toast:[NSString stringWithFormat:@"%@  ·  Esc for menu", g.title]];
}

- (void)flushBatterySave:(BOOL)force {
    if (!_emu || !_current) return;
    if (!_emu->get_bus().take_sram_dirty() && !force) return;
    auto &sram = _emu->get_bus().get_sram();
    NSData *d = [NSData dataWithBytes:sram.data() length:sram.size()];
    if (![d writeToURL:[_current batterySaveURL] atomically:YES]) NSLog(@"[Save] Could not write battery save");
}

- (void)closeGame:(id)sender {
    if (!_emu) { [_window performClose:sender]; return; }
    [self flushBatterySave:YES];
    [self writePNG:[_current coverURL]];
    [_current loadCover];
    _audio.pause();
    _emu.reset();
    _current = nil;
    _paused = NO;
    _root.gameMode = NO;
    _root.pause.hidden = YES;
    _window.title = @"Super A'Can Emulator";
    [self refreshLibrary];
    [_window makeFirstResponder:_root.grid];
    [NSCursor setHiddenUntilMouseMoves:NO];
}

- (void)setPaused:(BOOL)p showMenu:(BOOL)menu {
    if (!_emu) return;
    _paused = p;
    if (p) {
        _audio.pause();
        _root.pause.gameTitle = _current.title;
        [self updateSlotUI];
        _root.pause.hidden = !menu;
        _root.pause.needsLayout = YES; _root.pause.needsDisplay = YES;
    } else {
        _root.pause.hidden = YES;
        _audio.start();
        _last = CACurrentMediaTime(); _acc = 0;
        [_window makeFirstResponder:_root.game];
    }
}
- (void)togglePause:(id)s { [self setPaused:!_paused showMenu:YES]; }
- (void)resetGame:(id)s {
    if (!_emu) return;
    _emu->reset();
    if (_paused) [self setPaused:NO showMenu:NO];
    [self toast:@"Reset"];
}

// ── Save states ────────────────────────────────────────────────────────────
- (BOOL)slotExists:(int)slot { return _current && [NSFileManager.defaultManager fileExistsAtPath:[_current stateURL:slot].path]; }
- (void)updateSlotUI {
    _root.pause.slots.selectedSegment = _slot - 1;
    NSString *info = @"Empty";
    if ([self slotExists:_slot]) {
        NSDate *d = [[NSFileManager.defaultManager attributesOfItemAtPath:[_current stateURL:_slot].path error:nil] fileModificationDate];
        info = fabs(d.timeIntervalSinceNow) < 60 ? @"Saved just now"
             : [@"Saved " stringByAppendingString:[[NSRelativeDateTimeFormatter new] localizedStringForDate:d relativeToDate:[NSDate date]]];
    }
    _root.pause.slotInfo = info;
    _root.pause.buttons[2].enabled = [self slotExists:_slot];
    _root.pause.needsDisplay = YES;
}
- (void)slotSegmentChanged:(NSSegmentedControl *)s { _slot = (int)s.selectedSegment + 1; [self updateSlotUI]; }
- (void)selectSlot:(NSMenuItem *)i {
    _slot = (int)i.tag;
    [self updateSlotUI];
    if (_emu) [self toast:[NSString stringWithFormat:@"State slot %d%@", _slot, [self slotExists:_slot] ? @"" : @" (empty)"]];
}
- (void)saveState:(id)s {
    if (!_emu) return;
    if (SaveState::save(*_emu, [_current stateURL:_slot].fileSystemRepresentation)) {
        [self toast:[NSString stringWithFormat:@"Saved to slot %d", _slot]];
    } else {
        [self toast:@"Couldn't save state"];
    }
    if (_paused) [self setPaused:NO showMenu:NO];
}
- (void)loadState:(id)s {
    if (!_emu) return;
    if (![self slotExists:_slot]) { [self toast:[NSString stringWithFormat:@"Slot %d is empty", _slot]]; return; }
    if (SaveState::load(*_emu, [_current stateURL:_slot].fileSystemRepresentation)) {
        _audio.clear();
        [self flushBatterySave:YES];
        [self toast:[NSString stringWithFormat:@"Loaded slot %d", _slot]];
    } else {
        [self toast:@"Couldn't load state"];
    }
    if (_paused) [self setPaused:NO showMenu:NO];
    [self present];
}

// ── View options ───────────────────────────────────────────────────────────
- (void)applyScaling { _root.game.layer.magnificationFilter = _smooth ? kCAFilterLinear : kCAFilterNearest; }
- (void)toggleSmooth:(id)s {
    _smooth = !_smooth;
    [NSUserDefaults.standardUserDefaults setBool:_smooth forKey:@"smoothScaling"];
    [self applyScaling];
}
- (void)setSoundStyle:(NSMenuItem *)i {
    BOOL sustained = (i.tag == 1);
    [NSUserDefaults.standardUserDefaults setBool:sustained forKey:@"soundSustained"];
    if (_emu) _emu->get_apu().set_sustained(sustained);
    [self toast:sustained ? @"Sound: Sustained (like MAME)" : @"Sound: Accurate"];
}
- (void)toggleMute:(id)s { _audio.setMuted(!_audio.isMuted()); [self toast:_audio.isMuted() ? @"Audio muted" : @"Audio on"]; }
- (void)setWindowScale:(NSMenuItem *)i {
    if (_window.styleMask & NSWindowStyleMaskFullScreen) return;
    CGFloat titleH = _window.frame.size.height - _window.contentLayoutRect.size.height;
    NSSize want = NSMakeSize(320 * i.tag, 240 * i.tag + titleH);
    want.width = MAX(want.width, _window.minSize.width);
    want.height = MAX(want.height, _window.minSize.height);
    NSRect f = _window.frame;
    f.origin.y += f.size.height - want.height;
    f.size = want;
    [_window setFrame:f display:YES animate:YES];
}
- (void)about:(id)s {
    [NSApp orderFrontStandardAboutPanelWithOptions:@{
        NSAboutPanelOptionApplicationName: @"Super A'Can Emulator",
        NSAboutPanelOptionApplicationVersion: @"3.0",
        NSAboutPanelOptionCredits: [[NSAttributedString alloc] initWithString:
            @"A native macOS emulator for the Funtech Super A'Can (1995).\nMotorola 68000 core: Musashi."
            attributes:@{ NSFontAttributeName: F(11), NSForegroundColorAttributeName: NSColor.secondaryLabelColor }],
    }];
}

// ── Toast ──────────────────────────────────────────────────────────────────
- (void)toast:(NSString *)msg {
    _root.toast.message = msg;
    _root.toast.needsDisplay = YES;
    _root.needsLayout = YES;
    [_root layoutSubtreeIfNeeded];
    [_root addSubview:_root.toast positioned:NSWindowAbove relativeTo:nil];
    [NSAnimationContext runAnimationGroup:^(NSAnimationContext *c) { c.duration = 0.15; self->_root.toast.animator.alphaValue = 1; }];
    [_toastTimer invalidate];
    _toastTimer = [NSTimer scheduledTimerWithTimeInterval:1.8 repeats:NO block:^(NSTimer *t) {
        [NSAnimationContext runAnimationGroup:^(NSAnimationContext *c) { c.duration = 0.4; self->_root.toast.animator.alphaValue = 0; }];
    }];
}

// ── Input ──────────────────────────────────────────────────────────────────
static uint16_t ButtonForKey(unsigned short code) {
    switch (code) {
        case 126: return BTN_UP;
        case 125: return BTN_DOWN;
        case 123: return BTN_LEFT;
        case 124: return BTN_RIGHT;
        case 6:   return BTN_A;      // Z
        case 7:   return BTN_B;      // X
        case 0:   return BTN_X;      // A
        case 1:   return BTN_Y;      // S
        case 12:  return BTN_L;      // Q
        case 13:  return BTN_R;      // W
        case 36: case 76: return BTN_START; // Return / Enter
        case 49:  return BTN_SELECT; // Space
        default:  return 0;
    }
}
- (void)gameKey:(NSEvent *)e down:(BOOL)down {
    if (e.keyCode == 53) { if (down && !e.isARepeat) [self togglePause:nil]; return; } // Esc
    if (e.keyCode == 48) { _fastForward = down; return; }                              // Tab
    if (_paused) return;
    uint16_t b = ButtonForKey(e.keyCode);
    if (!b) return;
    if (down) _keys |= b; else _keys &= ~b;
    [NSCursor setHiddenUntilMouseMoves:YES];
}
- (uint16_t)padButtons {
    uint16_t out = 0;
    for (GCController *c in GCController.controllers) {
        GCExtendedGamepad *p = c.extendedGamepad;
        if (!p) continue;
        float x = p.leftThumbstick.xAxis.value, y = p.leftThumbstick.yAxis.value;
        if (p.dpad.up.pressed || y > 0.5f) out |= BTN_UP;
        if (p.dpad.down.pressed || y < -0.5f) out |= BTN_DOWN;
        if (p.dpad.left.pressed || x < -0.5f) out |= BTN_LEFT;
        if (p.dpad.right.pressed || x > 0.5f) out |= BTN_RIGHT;
        if (p.buttonA.pressed) out |= BTN_B;   // south
        if (p.buttonB.pressed) out |= BTN_A;   // east
        if (p.buttonX.pressed) out |= BTN_Y;   // west
        if (p.buttonY.pressed) out |= BTN_X;   // north
        if (p.leftShoulder.pressed) out |= BTN_L;
        if (p.rightShoulder.pressed) out |= BTN_R;
        if (p.buttonMenu.pressed) out |= BTN_START;
        if (p.buttonOptions.pressed) out |= BTN_SELECT;
        if (p.rightTrigger.pressed) _fastForward = YES;
    }
    return out;
}

// ── Frame loop ─────────────────────────────────────────────────────────────
- (void)stepFrame:(BOOL)withAudio {
    uint16_t buttons = _keys | [self padButtons];
    Joypad &pad = _emu->get_joypad();
    for (uint16_t b : kAllButtons) pad.set_button(0, static_cast<JoypadButton>(b), (buttons & b) != 0);
    _emu->get_apu().clear_audio_buffer();
    _emu->run_frame();
    if (withAudio) {
        const auto &buf = _emu->get_apu().get_audio_buffer();
        if (!buf.empty()) _audio.push(buf.data(), buf.size());
    }
}

- (void)tick {
    CFTimeInterval now = CACurrentMediaTime();
    double dt = MIN(now - _last, 0.1);
    _last = now;
    if (!_emu || _paused) return;

    double rate = 59.98;
    if (_fastForward) {
        rate *= 4.0;
    } else if (_audio.ok()) {
        // Gently steer emulation speed to keep ~60 ms of audio queued (no drift, no crackle).
        const double target = AudioOut::kRate * 0.06;
        double err = (target - (double)_audio.queuedFrames()) / target;
        rate *= 1.0 + std::clamp(err * 0.04, -0.04, 0.04);
    }
    _acc += dt * rate;
    int n = 0;
    while (_acc >= 1.0 && n < 10) { [self stepFrame:!_fastForward]; _acc -= 1.0; n++; }
    if (_acc > 3.0) _acc = 0;
    if (n) {
        static int flushCounter = 0;
        if ((flushCounter += n) >= 60) { flushCounter = 0; [self flushBatterySave:NO]; }
        [self present];
        _fpsFrames += n;
        if (now - _fpsStart >= 1.0) { _fps = _fpsFrames / (now - _fpsStart); _fpsFrames = 0; _fpsStart = now; }
    }
}

- (CGImageRef)copyFrameImage CF_RETURNS_RETAINED {
    const uint32_t *fb = _emu->get_framebuffer();
    CFDataRef data = CFDataCreate(nullptr, reinterpret_cast<const UInt8 *>(fb), 320 * 240 * 4);
    CGDataProviderRef prov = CGDataProviderCreateWithCFData(data);
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    // Rows stay 320 px apart; in the H256 mode only the first 256 columns are the picture.
    CGImageRef img = CGImageCreate(_emu->get_active_width(), 240, 8, 32, 320 * 4, cs,
                                   kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst,
                                   prov, nullptr, false, kCGRenderingIntentDefault);
    CGColorSpaceRelease(cs); CGDataProviderRelease(prov); CFRelease(data);
    return img;
}
- (void)present {
    if (!_emu) return;
    CGImageRef img = [self copyFrameImage];
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    _root.game.picture.contents = (__bridge id)img;
    [CATransaction commit];
    CGImageRelease(img);
}
- (void)writePNG:(NSURL *)url {
    if (!_emu || !url) return;
    CGImageRef img = [self copyFrameImage];
    NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithCGImage:img];
    CGImageRelease(img);
    [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToURL:url atomically:YES];
}

// ── Debug automation (only active when SCAN_DEBUG is set) ──────────────────
// SCAN_DEBUG="1.5:snap:/tmp/a.png;3:pause;3.5:snap:/tmp/b.png;4:close;5:select:0;6:quit"
- (void)runDebugScript {
    const char *env = getenv("SCAN_DEBUG");
    if (!env) return;
    for (NSString *cmd in [@(env) componentsSeparatedByString:@";"]) {
        NSArray *parts = [cmd componentsSeparatedByString:@":"];
        if (parts.count < 2) continue;
        double t = [parts[0] doubleValue];
        NSString *op = parts[1];
        NSString *arg = parts.count > 2 ? [[parts subarrayWithRange:NSMakeRange(2, parts.count - 2)] componentsJoinedByString:@":"] : nil;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(t * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            if ([op isEqualToString:@"snap"]) [self debugSnapshot:arg];
            else if ([op isEqualToString:@"pause"]) [self setPaused:YES showMenu:YES];
            else if ([op isEqualToString:@"resume"]) [self setPaused:NO showMenu:NO];
            else if ([op isEqualToString:@"close"]) [self closeGame:nil];
            else if ([op isEqualToString:@"save"]) [self saveState:nil];
            else if ([op isEqualToString:@"load"]) [self loadState:nil];
            else if ([op isEqualToString:@"hover"]) {
                NSInteger i = arg.integerValue;
                if (i < (NSInteger)self->_root.grid.cards.count) { self->_root.grid.cards[i].hover = YES; self->_root.grid.cards[i].needsDisplay = YES; }
            }
            else if ([op isEqualToString:@"select"]) {
                NSInteger i = arg.integerValue;
                if (i < (NSInteger)self->_root.grid.games.count) [self selectGame:self->_root.grid.games[i]];
            }
            else if ([op isEqualToString:@"play"]) {
                NSInteger i = arg.integerValue;
                if (i < (NSInteger)self->_root.grid.games.count) [self playGame:self->_root.grid.games[i]];
            }
            else if ([op isEqualToString:@"press"]) { self->_keys |= BTN_START; }
            else if ([op isEqualToString:@"release"]) { self->_keys = 0; }
            else if ([op isEqualToString:@"drop"]) { self->_root.drop.hidden = NO; }
            else if ([op isEqualToString:@"filter"]) { [self setFilter:arg.integerValue]; }
            else if ([op isEqualToString:@"quit"]) [NSApp terminate:nil];
        });
    }
}
- (void)debugSnapshot:(NSString *)path {
    [_root layoutSubtreeIfNeeded];
    if (_emu) { CGImageRef img = [self copyFrameImage]; _root.game.snapshotImage = img; CGImageRelease(img); }
    NSBitmapImageRep *rep = [_root bitmapImageRepForCachingDisplayInRect:_root.bounds];
    [_root cacheDisplayInRect:_root.bounds toBitmapImageRep:rep];
    _root.game.snapshotImage = nil;
    [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES];
}
@end

// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, const char *argv[]) {
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        app.activationPolicy = NSApplicationActivationPolicyRegular;
        SCApp *delegate = [SCApp new];
        app.delegate = delegate;
        // Allow `SuperACan path/to/rom.bin` from the command line.
        if (argc > 1 && argv[1][0] != '-') {
            NSURL *u = [NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]]];
            [delegate queueURLs:@[u]];
        }
        [app activateIgnoringOtherApps:YES];
        [app run];
    }
    return 0;
}
