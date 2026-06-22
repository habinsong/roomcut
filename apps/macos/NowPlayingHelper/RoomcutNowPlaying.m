// RoomcutNowPlaying.m — MediaRemote helper, loaded by /usr/bin/perl.
//
// Resolves Apple's private MediaRemote.framework at runtime (via CFBundle) and
// exposes a tiny set of entry points that the perl launcher installs as XSUBs:
//
//   np_get     read Now Playing once, print one JSON line, exit
//   np_stream  print a JSON line on every Now Playing change (CFRunLoop)
//   np_artwork read artwork once, print one track-scoped JSON line, exit
//   np_queue   read previous/next queue metadata once, print JSON, exit
//   np_send    send an MRCommand (env ROOMCUT_NP_COMMAND)
//   np_seek    set elapsed time (env ROOMCUT_NP_POSITION_US, microseconds)
//   np_test    exit(0) if Now Playing is reachable, exit(1) otherwise
//
// The main Roomcut app never links MediaRemote; this private API stays isolated
// in the child perl process. Mechanism referenced from ungive/mediaremote-adapter
// (BSD-3-Clause). Local display/control only — no network, no logging of metadata.
//
// SPDX-License-Identifier: BSD-3-Clause

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <CoreFoundation/CoreFoundation.h>
#import "MediaRemote.h"

// Now Playing info dictionary keys (literal runtime keys).
static NSString *const kKeyTitle        = @"kMRMediaRemoteNowPlayingInfoTitle";
static NSString *const kKeyArtist       = @"kMRMediaRemoteNowPlayingInfoArtist";
static NSString *const kKeyAlbum        = @"kMRMediaRemoteNowPlayingInfoAlbum";
static NSString *const kKeyArtworkData  = @"kMRMediaRemoteNowPlayingInfoArtworkData";
static NSString *const kKeyArtworkMIME  = @"kMRMediaRemoteNowPlayingInfoArtworkMIMEType";
static NSString *const kKeyDuration     = @"kMRMediaRemoteNowPlayingInfoDuration";
static NSString *const kKeyElapsedTime  = @"kMRMediaRemoteNowPlayingInfoElapsedTime";
static NSString *const kKeyTimestamp    = @"kMRMediaRemoteNowPlayingInfoTimestamp";
static NSString *const kKeyPlaybackRate = @"kMRMediaRemoteNowPlayingInfoPlaybackRate";

#pragma mark - Framework loading

static CFBundleRef gBundle = NULL;
static MRMediaRemoteGetNowPlayingInfo_t pGetInfo = NULL;
static MRMediaRemoteGetNowPlayingApplicationIsPlaying_t pIsPlaying = NULL;
static MRMediaRemoteGetNowPlayingApplicationPID_t pGetPID = NULL;
static MRMediaRemoteSendCommand_t pSendCommand = NULL;
static MRMediaRemoteSetElapsedTime_t pSetElapsed = NULL;
static MRMediaRemoteRegisterForNowPlayingNotifications_t pRegister = NULL;
static MRPlaybackQueueRequestCreateDefaultWithRange_t pCreateQueueRequestWithRange = NULL;
static MRPlaybackQueueRequestSetIncludeMetadata_t pQueueRequestSetIncludeMetadata = NULL;
static MRPlaybackQueueRequestSetIncludeInfo_t pQueueRequestSetIncludeInfo = NULL;
static MRMediaRemoteRequestNowPlayingPlaybackQueueSync_t pRequestQueue = NULL;
static MRPlaybackQueueGetRange_t pQueueGetRange = NULL;
static MRPlaybackQueueCopyContentItems_t pQueueCopyContentItems = NULL;
static MRPlaybackQueueGetContentItemAtOffset_t pQueueGetContentItemAtOffset = NULL;
static MRContentItemGetTitle_t pContentItemGetTitle = NULL;
static MRContentItemGetTrackArtistName_t pContentItemGetArtist = NULL;
static MRContentItemGetAlbumName_t pContentItemGetAlbum = NULL;
static MRContentItemGetDuration_t pContentItemGetDuration = NULL;
static MRContentItemGetIsCurrentlyPlaying_t pContentItemGetIsCurrent = NULL;

static void *symbol(const char *name) {
    return CFBundleGetFunctionPointerForName(
        gBundle, (__bridge CFStringRef)[NSString stringWithUTF8String:name]);
}

// Returns YES on success. Resolves only the symbols we actually use.
static BOOL loadFramework(void) {
    if (gBundle) return pGetInfo != NULL;
    NSURL *url = [NSURL fileURLWithPath:
        @"/System/Library/PrivateFrameworks/MediaRemote.framework"];
    gBundle = CFBundleCreate(kCFAllocatorDefault, (__bridge CFURLRef)url);
    if (!gBundle) return NO;
    pGetInfo    = (MRMediaRemoteGetNowPlayingInfo_t)symbol("MRMediaRemoteGetNowPlayingInfo");
    pIsPlaying  = (MRMediaRemoteGetNowPlayingApplicationIsPlaying_t)symbol("MRMediaRemoteGetNowPlayingApplicationIsPlaying");
    pGetPID     = (MRMediaRemoteGetNowPlayingApplicationPID_t)symbol("MRMediaRemoteGetNowPlayingApplicationPID");
    pSendCommand = (MRMediaRemoteSendCommand_t)symbol("MRMediaRemoteSendCommand");
    pSetElapsed = (MRMediaRemoteSetElapsedTime_t)symbol("MRMediaRemoteSetElapsedTime");
    pRegister   = (MRMediaRemoteRegisterForNowPlayingNotifications_t)symbol("MRMediaRemoteRegisterForNowPlayingNotifications");
    pCreateQueueRequestWithRange = (MRPlaybackQueueRequestCreateDefaultWithRange_t)symbol("MRPlaybackQueueRequestCreateDefaultWithRange");
    pQueueRequestSetIncludeMetadata = (MRPlaybackQueueRequestSetIncludeMetadata_t)symbol("MRPlaybackQueueRequestSetIncludeMetadata");
    pQueueRequestSetIncludeInfo = (MRPlaybackQueueRequestSetIncludeInfo_t)symbol("MRPlaybackQueueRequestSetIncludeInfo");
    pRequestQueue = (MRMediaRemoteRequestNowPlayingPlaybackQueueSync_t)symbol("MRMediaRemoteRequestNowPlayingPlaybackQueueSync");
    pQueueGetRange = (MRPlaybackQueueGetRange_t)symbol("MRPlaybackQueueGetRange");
    pQueueCopyContentItems = (MRPlaybackQueueCopyContentItems_t)symbol("MRPlaybackQueueCopyContentItems");
    pQueueGetContentItemAtOffset = (MRPlaybackQueueGetContentItemAtOffset_t)symbol("MRPlaybackQueueGetContentItemAtOffset");
    pContentItemGetTitle = (MRContentItemGetTitle_t)symbol("MRContentItemGetTitle");
    pContentItemGetArtist = (MRContentItemGetTrackArtistName_t)symbol("MRContentItemGetTrackArtistName");
    pContentItemGetAlbum = (MRContentItemGetAlbumName_t)symbol("MRContentItemGetAlbumName");
    pContentItemGetDuration = (MRContentItemGetDuration_t)symbol("MRContentItemGetDuration");
    pContentItemGetIsCurrent = (MRContentItemGetIsCurrentlyPlaying_t)symbol("MRContentItemGetIsCurrentlyPlaying");
    return pGetInfo != NULL;
}

#pragma mark - Payload

typedef NS_ENUM(NSInteger, PayloadKind) {
    PayloadKindMetadata,
    PayloadKindArtwork,
    PayloadKindFull,
    PayloadKindStream,   // metadata + artwork ONLY when the cover is new (diffed)
};

// np_stream diffing: the track key whose artwork we last EMITTED. The cover is a
// hundreds-of-KB base64 blob and the heaviest, last-arriving field; re-shipping it
// on every InfoDidChange floods the pipe and lags/spikes the consumer. So in stream
// mode we include it only when the track changes (or its art first appears).
static NSString *gStreamLastArtworkKey = nil;

static NSString *trackKeyForInfo(NSDictionary *info) {
    NSNumber *duration = info[kKeyDuration];
    return [NSString stringWithFormat:@"%@|%@|%.0f",
        info[kKeyTitle] ?: @"", info[kKeyArtist] ?: @"", duration.doubleValue];
}

static NSDictionary *buildMetadataPayload(
    NSDictionary *info, BOOL hasPlaying, BOOL isPlaying, int pid
) {
    NSMutableDictionary *out = [NSMutableDictionary dictionary];
    id v;
    if ((v = info[kKeyTitle]))  out[@"title"]  = v;
    if ((v = info[kKeyArtist])) out[@"artist"] = v;
    if ((v = info[kKeyAlbum]))  out[@"album"]  = v;
    if ((v = info[kKeyDuration]) && [v isKindOfClass:NSNumber.class])
        out[@"duration"] = v;
    if ((v = info[kKeyElapsedTime]) && [v isKindOfClass:NSNumber.class])
        out[@"elapsedTime"] = v;
    if ((v = info[kKeyPlaybackRate]) && [v isKindOfClass:NSNumber.class])
        out[@"playbackRate"] = v;
    if ((v = info[kKeyTimestamp]) && [v isKindOfClass:NSDate.class])
        out[@"timestamp"] = @([(NSDate *)v timeIntervalSince1970]);
    out[@"trackKey"] = trackKeyForInfo(info);

    if (hasPlaying) out[@"playing"] = @(isPlaying);

    if (pid > 0) {
        NSRunningApplication *app =
            [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
        if (app.bundleIdentifier) out[@"bundleIdentifier"] = app.bundleIdentifier;
        if (app.localizedName)    out[@"appName"] = app.localizedName;
    }
    return out;
}

static NSDictionary *buildArtworkPayload(NSDictionary *info) {
    NSMutableDictionary *out = [NSMutableDictionary dictionary];
    out[@"trackKey"] = trackKeyForInfo(info);
    NSData *art = info[kKeyArtworkData];
    if ([art isKindOfClass:NSData.class] && art.length > 0) {
        out[@"artworkData"] = [art base64EncodedStringWithOptions:0];
        id mime = info[kKeyArtworkMIME];
        if ([mime isKindOfClass:NSString.class]) out[@"artworkMimeType"] = mime;
    }
    return out;
}

static void emitJSON(NSDictionary *payload) {
    NSError *err = nil;
    NSData *data = [NSJSONSerialization dataWithJSONObject:payload options:0 error:&err];
    if (!data) return;
    NSMutableData *line = [data mutableCopy];
    [line appendBytes:"\n" length:1];
    fwrite(line.bytes, 1, line.length, stdout);
    fflush(stdout);
}

// Fetches info + isPlaying + pid synchronously (with timeout). Returns the
// merged payload, or nil on failure/timeout.
static NSDictionary *fetchOnce(NSTimeInterval timeout, PayloadKind kind) {
    if (!loadFramework()) return nil;
    dispatch_queue_t q = dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0);
    dispatch_group_t group = dispatch_group_create();

    __block NSDictionary *info = nil;
    __block BOOL isPlaying = NO;
    __block BOOL hasPlaying = NO;
    __block int pid = -1;

    dispatch_group_enter(group);
    pGetInfo(q, ^(NSDictionary *information) {
        info = information;
        dispatch_group_leave(group);
    });

    if (pIsPlaying) {
        dispatch_group_enter(group);
        pIsPlaying(q, ^(BOOL playing) {
            isPlaying = playing;
            hasPlaying = YES;
            dispatch_group_leave(group);
        });
    }
    if (pGetPID) {
        dispatch_group_enter(group);
        pGetPID(q, ^(int p) {
            pid = p;
            dispatch_group_leave(group);
        });
    }

    long timedOut = dispatch_group_wait(
        group, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(timeout * NSEC_PER_SEC)));
    if (timedOut != 0) return nil;
    if (!info) return nil;
    if (kind == PayloadKindArtwork) return buildArtworkPayload(info);
    NSDictionary *metadata = buildMetadataPayload(info, hasPlaying, isPlaying, pid);
    if (kind == PayloadKindMetadata) return metadata;
    if (kind == PayloadKindStream) {
        NSData *art = info[kKeyArtworkData];
        BOOL hasArt = [art isKindOfClass:NSData.class] && art.length > 0;
        NSString *tk = trackKeyForInfo(info);
        // Ship the cover only the first time we see it for this track; otherwise
        // emit lightweight metadata so position/state updates stay cheap.
        if (hasArt && ![tk isEqualToString:gStreamLastArtworkKey]) {
            gStreamLastArtworkKey = [tk copy];
            NSMutableDictionary *full = [metadata mutableCopy];
            [full addEntriesFromDictionary:buildArtworkPayload(info)];
            return full;
        }
        return metadata;
    }
    NSMutableDictionary *full = [metadata mutableCopy];
    [full addEntriesFromDictionary:buildArtworkPayload(info)];
    return full;
}

static NSDictionary *fetchQueueOnce(NSTimeInterval timeout) {
    if (!loadFramework()
        || !pCreateQueueRequestWithRange
        || !pQueueRequestSetIncludeMetadata
        || !pRequestQueue
        || (!pQueueCopyContentItems && !pQueueGetContentItemAtOffset)
        || !pContentItemGetTitle
        || !pContentItemGetArtist
        || !pContentItemGetAlbum
        || !pContentItemGetDuration
        || !pContentItemGetIsCurrent) {
        return nil;
    }

    dispatch_queue_t q = dispatch_get_global_queue(QOS_CLASS_UTILITY, 0);
    dispatch_semaphore_t infoSemaphore = dispatch_semaphore_create(0);
    __block NSDictionary *info = nil;
    pGetInfo(q, ^(NSDictionary *information) {
        info = information;
        dispatch_semaphore_signal(infoSemaphore);
    });
    long infoTimedOut = dispatch_semaphore_wait(
        infoSemaphore,
        dispatch_time(DISPATCH_TIME_NOW, (int64_t)(timeout * NSEC_PER_SEC))
    );
    if (infoTimedOut != 0 || !info) return nil;

    NSNumber *queueIndexValue = info[kMRMediaRemoteNowPlayingInfoQueueIndex];
    NSNumber *queueCountValue = info[kMRMediaRemoteNowPlayingInfoTotalQueueCount];
    BOOL hasQueueIndex = [queueIndexValue isKindOfClass:NSNumber.class];
    NSInteger queueIndex = hasQueueIndex ? queueIndexValue.integerValue : NSNotFound;

    id request = nil;
    if (hasQueueIndex) {
        NSUInteger start = queueIndex > 0 ? (NSUInteger)queueIndex - 1 : 0;
        NSUInteger length = 3;
        if ([queueCountValue isKindOfClass:NSNumber.class]) {
            NSUInteger total = MAX(0, queueCountValue.integerValue);
            length = total > start ? MIN(3, total - start) : 0;
        }
        if (length > 0) {
            request = pCreateQueueRequestWithRange(NSMakeRange(start, length));
        }
    }
    if (!request) {
        request = pCreateQueueRequestWithRange(
            NSMakeRange((NSUInteger)(NSInteger)-1, 3)
        );
    }
    if (!request) return nil;
    pQueueRequestSetIncludeMetadata(request, YES);
    if (pQueueRequestSetIncludeInfo) pQueueRequestSetIncludeInfo(request, YES);

    dispatch_semaphore_t queueSemaphore = dispatch_semaphore_create(0);
    __block id playbackQueue = nil;
    __block NSError *queueError = nil;
    pRequestQueue(request, q, ^(id result, NSError *error) {
        playbackQueue = result;
        queueError = error;
        dispatch_semaphore_signal(queueSemaphore);
    });
    long queueTimedOut = dispatch_semaphore_wait(
        queueSemaphore,
        dispatch_time(DISPATCH_TIME_NOW, (int64_t)(timeout * NSEC_PER_SEC))
    );
    if (queueTimedOut != 0 || queueError || !playbackQueue) return nil;

    NSArray *items = pQueueCopyContentItems
        ? pQueueCopyContentItems(playbackQueue)
        : @[];
    NSRange responseRange = pQueueGetRange
        ? pQueueGetRange(playbackQueue)
        : NSMakeRange(NSNotFound, 0);

    NSInteger currentPosition = NSNotFound;
    for (NSUInteger index = 0; index < items.count; index++) {
        if (pContentItemGetIsCurrent(items[index])) {
            currentPosition = (NSInteger)index;
            break;
        }
    }
    NSInteger relativeLocation = (NSInteger)responseRange.location;
    if (currentPosition == NSNotFound
        && relativeLocation <= 0
        && relativeLocation + (NSInteger)items.count > 0) {
        currentPosition = -relativeLocation;
    }
    if (currentPosition == NSNotFound
        && hasQueueIndex
        && responseRange.location != NSNotFound
        && queueIndex >= (NSInteger)responseRange.location
        && queueIndex < (NSInteger)(responseRange.location + items.count)) {
        currentPosition = queueIndex - (NSInteger)responseRange.location;
    }
    NSMutableArray *output = [NSMutableArray arrayWithCapacity:2];
    for (NSNumber *offsetValue in @[@(-1), @(1)]) {
        NSInteger offset = offsetValue.integerValue;
        id item = pQueueGetContentItemAtOffset
            ? pQueueGetContentItemAtOffset(playbackQueue, offset)
            : nil;
        if (!item && currentPosition != NSNotFound) {
            NSInteger position = currentPosition + offset;
            if (position >= 0 && position < (NSInteger)items.count) {
                item = items[(NSUInteger)position];
            }
        }
        if (!item) continue;
        NSString *title = pContentItemGetTitle(item);
        if (![title isKindOfClass:NSString.class] || title.length == 0) continue;

        NSMutableDictionary *payload = [NSMutableDictionary dictionary];
        payload[@"relativeOffset"] = @(offset);
        payload[@"title"] = title;
        NSString *artist = pContentItemGetArtist(item);
        NSString *album = pContentItemGetAlbum(item);
        if ([artist isKindOfClass:NSString.class]) payload[@"artist"] = artist;
        if ([album isKindOfClass:NSString.class]) payload[@"album"] = album;
        double duration = pContentItemGetDuration(item);
        if (duration > 0) payload[@"duration"] = @(duration);
        [output addObject:payload];
    }
    return @{@"items": output};
}

#pragma mark - Entry points (installed as perl XSUBs)

// These are called by perl via dl_install_xsub. Perl passes XSUB args we ignore;
// declaring (void) and never reading them is safe on the C ABI.

void np_get(void) {
    @autoreleasepool {
        NSDictionary *payload = fetchOnce(5.0, PayloadKindFull);
        if (!payload) exit(1);
        emitJSON(payload);
        exit(0);
    }
}

void np_artwork(void) {
    @autoreleasepool {
        NSDictionary *payload = fetchOnce(5.0, PayloadKindArtwork);
        if (!payload) exit(1);
        emitJSON(payload);
        exit(0);
    }
}

void np_queue(void) {
    @autoreleasepool {
        NSDictionary *payload = fetchQueueOnce(5.0);
        if (!payload) exit(1);
        emitJSON(payload);
        exit(0);
    }
}

void np_test(void) {
    @autoreleasepool {
        if (!loadFramework()) exit(1);
        // A reachable Now Playing service returns a (possibly empty) dict
        // without erroring. Treat any non-timeout response as success.
        dispatch_queue_t q = dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0);
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        pGetInfo(q, ^(NSDictionary *information) {
            dispatch_semaphore_signal(sem);
        });
        long timedOut = dispatch_semaphore_wait(
            sem, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(5.0 * NSEC_PER_SEC)));
        exit(timedOut == 0 ? 0 : 1);
    }
}

void np_send(void) {
    @autoreleasepool {
        if (!loadFramework() || !pSendCommand) exit(1);
        const char *cmd = getenv("ROOMCUT_NP_COMMAND");
        if (!cmd) exit(1);
        MRCommand command = (MRCommand)atoi(cmd);
        Boolean ok = pSendCommand(command, nil);
        // SendCommand dispatches asynchronously to mediaremoted; exiting
        // immediately can race the delivery. Give it a brief moment to land
        // (matches the adapter's post-send settle).
        [NSThread sleepForTimeInterval:0.2];
        exit(ok ? 0 : 1);
    }
}

void np_seek(void) {
    @autoreleasepool {
        if (!loadFramework() || !pSetElapsed) exit(1);
        const char *posUS = getenv("ROOMCUT_NP_POSITION_US");
        if (!posUS) exit(1);
        double seconds = atof(posUS) / 1.0e6;
        pSetElapsed(seconds);
        [NSThread sleepForTimeInterval:0.2];
        exit(0);
    }
}

void np_stream(void) {
    @autoreleasepool {
        if (!loadFramework()) exit(1);
        if (pRegister) {
            pRegister(dispatch_get_main_queue());
        }
        // Emit an initial snapshot so consumers don't wait for the first change.
        NSDictionary *initial = fetchOnce(5.0, PayloadKindStream);
        if (initial) emitJSON(initial);

        void (^onChange)(NSNotification *) = ^(NSNotification *note) {
            @autoreleasepool {
                // InfoDidChange → metadata + (diffed) artwork; play-state change →
                // metadata only. Never re-ships the cover unless the track changed.
                PayloadKind kind = [note.name isEqualToString:@"kMRMediaRemoteNowPlayingInfoDidChangeNotification"]
                    ? PayloadKindStream
                    : PayloadKindMetadata;
                NSDictionary *payload = fetchOnce(5.0, kind);
                if (payload) emitJSON(payload);
            }
        };
        NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
        [nc addObserverForName:@"kMRMediaRemoteNowPlayingInfoDidChangeNotification"
                        object:nil queue:nil usingBlock:onChange];
        [nc addObserverForName:@"kMRMediaRemoteNowPlayingApplicationIsPlayingDidChangeNotification"
                        object:nil queue:nil usingBlock:onChange];
        // Terminate cleanly if stdout closes (parent went away).
        signal(SIGPIPE, SIG_DFL);
        // Watchdog: if the parent app dies, we get reparented to launchd (ppid
        // becomes 1). Poll for that so a crashed app never leaks this child.
        pid_t startParent = getppid();
        dispatch_source_t wd = dispatch_source_create(
            DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
        dispatch_source_set_timer(wd, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC),
                                  2 * NSEC_PER_SEC, NSEC_PER_SEC);
        dispatch_source_set_event_handler(wd, ^{
            if (getppid() != startParent || getppid() == 1) {
                exit(0);
            }
        });
        dispatch_resume(wd);
        CFRunLoopRun();
        exit(0);
    }
}
