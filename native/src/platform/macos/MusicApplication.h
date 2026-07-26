#pragma once

// Hand-written minimal Scripting Bridge protocol for Music.app, covering
// only the properties this app reads. Normally generated via
// `sdef /Applications/Music.app | sdp -fh --basename Music -o .` on a real
// Mac - not possible in this environment (no macOS build machine
// available). Apple has kept these property names and the playback-state
// four-char codes stable since the iTunes-app days, so hand-declaring them
// is the same approach several open-source "now playing" tools use, but
// this has NOT been verified against a real Music.app scripting dictionary.
// If a property here is wrong/renamed, GetCurrentTrack's @try/@catch around
// the KVC access will surface it as "nothing playing" rather than a crash -
// regenerate this header from a real Mac (command above) to get an
// authoritative version.

#import <ScriptingBridge/ScriptingBridge.h>

typedef NS_ENUM(NSInteger, MusicEPlS) {
    MusicEPlSStopped = 'kPSS',
    MusicEPlSPlaying = 'kPSP',
    MusicEPlSPaused = 'kPSp',
    MusicEPlSFastForwarding = 'kPSF',
    MusicEPlSRewinding = 'kPSR'
};

@protocol MusicTrack <SBObject>
@property (copy, readonly) NSString *name;
@property (copy, readonly) NSString *artist;
@property (copy, readonly) NSString *album;
@property (readonly) double duration;
@property (readonly) NSInteger trackNumber;
@property (readonly) NSInteger trackCount;
@end

@protocol MusicApplication <SBApplicationProtocol>
@property (readonly) MusicEPlS playerState;
@property (readonly) double playerPosition;
@property (readonly, copy) id <MusicTrack> currentTrack;
@end
