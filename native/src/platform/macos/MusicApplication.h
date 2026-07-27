#pragma once

// Hand-written minimal Scripting Bridge protocol for Music.app, covering
// only the properties this app reads. Normally generated via
// `sdef /Applications/Music.app | sdp -fh --basename Music -o .` on a real
// Mac. Apple has kept these property names and the playback-state
// four-char codes stable since the iTunes-app days, so hand-declaring them
// is the same approach several open-source "now playing" tools use. If a
// property here is wrong/renamed, GetCurrentTrack's @try/@catch around the
// KVC access will surface it as "nothing playing" rather than a crash.
//
// Conforms to <NSObject>, not <SBObject>/<SBApplicationProtocol> - despite
// looking similar, those aren't protocols in the current SDK (SBObject and
// SBApplication are plain classes, @interface not @protocol - confirmed by
// grepping the real ScriptingBridge.framework headers on macOS 15.7). This
// compiled cleanly the first time this project was ever built on real Mac
// hardware; it never would have with the old declaration.

#import <ScriptingBridge/ScriptingBridge.h>

typedef NS_ENUM(NSInteger, MusicEPlS) {
    MusicEPlSStopped = 'kPSS',
    MusicEPlSPlaying = 'kPSP',
    MusicEPlSPaused = 'kPSp',
    MusicEPlSFastForwarding = 'kPSF',
    MusicEPlSRewinding = 'kPSR'
};

@protocol MusicTrack <NSObject>
@property (copy, readonly) NSString *name;
@property (copy, readonly) NSString *artist;
@property (copy, readonly) NSString *album;
@property (readonly) double duration;
@property (readonly) NSInteger trackNumber;
@property (readonly) NSInteger trackCount;
@end

@protocol MusicApplication <NSObject>
@property (readonly) MusicEPlS playerState;
@property (readonly) double playerPosition;
@property (readonly, copy) id <MusicTrack> currentTrack;
@end
