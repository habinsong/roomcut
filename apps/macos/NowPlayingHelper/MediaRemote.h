// MediaRemote.h — private MediaRemote.framework symbol declarations.
//
// These are reverse-engineered declarations of Apple's private MediaRemote
// framework. They are NOT linked at compile time; the helper resolves them at
// runtime via CFBundleGetFunctionPointerForName so the main app stays clean.
//
// Symbol declarations referenced from the public reverse-engineering work in
// ungive/mediaremote-adapter (BSD-3-Clause) and widely published MediaRemote
// headers. Used for local display/control only.
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ROOMCUT_MEDIAREMOTE_H
#define ROOMCUT_MEDIAREMOTE_H

#import <Foundation/Foundation.h>

// MRCommand identifiers (subset we use).
typedef NS_ENUM(NSInteger, MRCommand) {
    kMRPlay = 0,
    kMRPause = 1,
    kMRTogglePlayPause = 2,
    kMRStop = 3,
    kMRNextTrack = 4,
    kMRPreviousTrack = 5,
};

// Function pointer typedefs for the private symbols.
typedef void (*MRMediaRemoteGetNowPlayingInfo_t)(
    dispatch_queue_t queue, void (^handler)(NSDictionary *information));

typedef void (*MRMediaRemoteGetNowPlayingApplicationIsPlaying_t)(
    dispatch_queue_t queue, void (^handler)(BOOL isPlaying));

typedef void (*MRMediaRemoteGetNowPlayingApplicationPID_t)(
    dispatch_queue_t queue, void (^handler)(int pid));

typedef Boolean (*MRMediaRemoteSendCommand_t)(MRCommand command, NSDictionary *userInfo);

typedef void (*MRMediaRemoteSetElapsedTime_t)(double elapsedTime);

typedef void (*MRMediaRemoteRegisterForNowPlayingNotifications_t)(dispatch_queue_t queue);

typedef id (*MRPlaybackQueueRequestCreateDefaultWithRange_t)(NSRange range);
typedef void (*MRPlaybackQueueRequestSetIncludeMetadata_t)(id request, BOOL includeMetadata);
typedef void (*MRPlaybackQueueRequestSetIncludeInfo_t)(id request, BOOL includeInfo);
typedef void (*MRMediaRemoteRequestNowPlayingPlaybackQueueSync_t)(
    id request,
    dispatch_queue_t queue,
    void (^handler)(id playbackQueue, NSError *error)
);
typedef NSRange (*MRPlaybackQueueGetRange_t)(id playbackQueue);
typedef NSArray *(*MRPlaybackQueueCopyContentItems_t)(id playbackQueue);
typedef id (*MRPlaybackQueueGetContentItemAtOffset_t)(
    id playbackQueue,
    NSInteger offset
);
typedef NSString *(*MRContentItemGetTitle_t)(id contentItem);
typedef NSString *(*MRContentItemGetTrackArtistName_t)(id contentItem);
typedef NSString *(*MRContentItemGetAlbumName_t)(id contentItem);
typedef double (*MRContentItemGetDuration_t)(id contentItem);
typedef BOOL (*MRContentItemGetIsCurrentlyPlaying_t)(id contentItem);

// Now Playing info dictionary keys (subset).
// These resolve to CFStringRef * symbols inside the framework.
#define kMRMediaRemoteNowPlayingInfoTitle              @"kMRMediaRemoteNowPlayingInfoTitle"
#define kMRMediaRemoteNowPlayingInfoArtist             @"kMRMediaRemoteNowPlayingInfoArtist"
#define kMRMediaRemoteNowPlayingInfoAlbum              @"kMRMediaRemoteNowPlayingInfoAlbum"
#define kMRMediaRemoteNowPlayingInfoArtworkData        @"kMRMediaRemoteNowPlayingInfoArtworkData"
#define kMRMediaRemoteNowPlayingInfoArtworkMIMEType    @"kMRMediaRemoteNowPlayingInfoArtworkMIMEType"
#define kMRMediaRemoteNowPlayingInfoDuration           @"kMRMediaRemoteNowPlayingInfoDuration"
#define kMRMediaRemoteNowPlayingInfoElapsedTime        @"kMRMediaRemoteNowPlayingInfoElapsedTime"
#define kMRMediaRemoteNowPlayingInfoTimestamp          @"kMRMediaRemoteNowPlayingInfoTimestamp"
#define kMRMediaRemoteNowPlayingInfoPlaybackRate       @"kMRMediaRemoteNowPlayingInfoPlaybackRate"
#define kMRMediaRemoteNowPlayingInfoQueueIndex         @"kMRMediaRemoteNowPlayingInfoQueueIndex"
#define kMRMediaRemoteNowPlayingInfoTotalQueueCount    @"kMRMediaRemoteNowPlayingInfoTotalQueueCount"

#endif // ROOMCUT_MEDIAREMOTE_H
