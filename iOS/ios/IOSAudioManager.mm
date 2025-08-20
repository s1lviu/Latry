/*
 * Copyright (C) 2025 Silviu YO6SAY
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#import "IOSAudioManager.h"
#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>

// Forward declaration of C++ function
#ifdef __cplusplus
extern "C" {
#endif
void handleIOSAudioInterruption(int type);
#ifdef __cplusplus
}
#endif

@implementation IOSAudioManager

+ (instancetype)sharedInstance {
    static IOSAudioManager *sharedInstance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sharedInstance = [[IOSAudioManager alloc] init];
    });
    return sharedInstance;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _backgroundTask = UIBackgroundTaskInvalid;
        _audioSession = [AVAudioSession sharedInstance];
        _screenWakeLockActive = NO;
        
        // Register for audio session interruption notifications
        [[NSNotificationCenter defaultCenter] addObserver:self
                                                 selector:@selector(handleAudioSessionInterruption:)
                                                     name:AVAudioSessionInterruptionNotification
                                                   object:nil];
        
        // Register for app state changes
        [[NSNotificationCenter defaultCenter] addObserver:self
                                                 selector:@selector(appDidEnterBackground:)
                                                     name:UIApplicationDidEnterBackgroundNotification
                                                   object:nil];
        
        [[NSNotificationCenter defaultCenter] addObserver:self
                                                 selector:@selector(appWillEnterForeground:)
                                                     name:UIApplicationWillEnterForegroundNotification
                                                   object:nil];
    }
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [self endBackgroundTask];
    [self releaseScreenWakeLock];
}

- (void)configureVoIPAudioSession {
    NSError *error = nil;
    
    // Step 1: Set preferences BEFORE activating session (Apple recommendation)
    NSLog(@"IOSAudioManager: Setting VoIP preferences before activation");
    
    // Set preferred sample rate - try common VoIP rates in order of preference
    // Apple docs: "Different hardware may have different capabilities"
    double preferredRates[] = {16000.0, 24000.0, 48000.0}; // VoIP optimal rates
    for (int i = 0; i < 3; i++) {
        [self.audioSession setPreferredSampleRate:preferredRates[i] error:&error];
        if (!error) {
            NSLog(@"IOSAudioManager: Preferred sample rate set to %.0fHz", preferredRates[i]);
            break;
        }
    }
    
    // Set preferred buffer duration for low latency VoIP
    [self.audioSession setPreferredIOBufferDuration:0.02 error:&error];
    if (error) {
        NSLog(@"IOSAudioManager: Failed to set preferred buffer duration: %@", error.localizedDescription);
    }
    
    // Using software gain boost in AudioEngine for consistent volume levels
    NSLog(@"IOSAudioManager: Using software gain boost for iOS transmission volume");
    
    // Step 2: Configure audio session for VoIP (before activation)
    [self.audioSession setCategory:AVAudioSessionCategoryPlayAndRecord
                              mode:AVAudioSessionModeVoiceChat
                           options:AVAudioSessionCategoryOptionAllowBluetooth |
                                   AVAudioSessionCategoryOptionDefaultToSpeaker |
                                   AVAudioSessionCategoryOptionAllowBluetoothA2DP
                             error:&error];
    
    if (error) {
        NSLog(@"IOSAudioManager: Failed to set audio session category: %@", error.localizedDescription);
        return;
    }
    
    NSLog(@"IOSAudioManager: VoIP audio session configured successfully");
}

- (void)activateAudioSession {
    NSError *error = nil;
    [self.audioSession setActive:YES error:&error];
    
    if (error) {
        NSLog(@"IOSAudioManager: Failed to activate audio session: %@", error.localizedDescription);
    } else {
        // Log actual values that iOS selected after activation
        double actualSampleRate = self.audioSession.sampleRate;
        NSTimeInterval actualBufferDuration = self.audioSession.IOBufferDuration;
        NSInteger inputChannels = self.audioSession.inputNumberOfChannels;
        NSInteger outputChannels = self.audioSession.outputNumberOfChannels;
        
        NSLog(@"IOSAudioManager: VoIP audio session activated successfully");
        NSLog(@"IOSAudioManager: Actual sample rate: %.0fHz (hardware selected)", actualSampleRate);
        NSLog(@"IOSAudioManager: Actual buffer duration: %.3fs", actualBufferDuration);
        NSLog(@"IOSAudioManager: Input channels: %ld, Output channels: %ld", (long)inputChannels, (long)outputChannels);
        
        // Check if we got our preferred rate
        if (actualSampleRate != 16000.0 && actualSampleRate != 24000.0 && actualSampleRate != 48000.0) {
            NSLog(@"IOSAudioManager: WARNING - Hardware selected non-VoIP sample rate: %.0fHz", actualSampleRate);
        }
    }
}

- (void)deactivateAudioSession {
    NSError *error = nil;
    [self.audioSession setActive:NO 
                     withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation 
                           error:&error];
    
    if (error) {
        NSLog(@"IOSAudioManager: Failed to deactivate audio session: %@", error.localizedDescription);
    } else {
        NSLog(@"IOSAudioManager: Audio session deactivated");
    }
}

- (void)handleAudioSessionInterruption:(NSNotification *)notification {
    NSNumber *interruptionTypeNumber = notification.userInfo[AVAudioSessionInterruptionTypeKey];
    AVAudioSessionInterruptionType interruptionType = (AVAudioSessionInterruptionType)[interruptionTypeNumber unsignedIntegerValue];
    
    switch (interruptionType) {
        case AVAudioSessionInterruptionTypeBegan:
            NSLog(@"IOSAudioManager: Audio session interruption began");
            // Notify Qt/C++ code that audio was interrupted (equivalent to Android audio focus loss)
            ios_handleAudioSessionInterruption(0); // 0 = interruption began
            break;
            
        case AVAudioSessionInterruptionTypeEnded: {
            NSLog(@"IOSAudioManager: Audio session interruption ended");
            
            NSNumber *optionsNumber = notification.userInfo[AVAudioSessionInterruptionOptionKey];
            AVAudioSessionInterruptionOptions options = (AVAudioSessionInterruptionOptions)[optionsNumber unsignedIntegerValue];
            
            if (options & AVAudioSessionInterruptionOptionShouldResume) {
                // Reactivate audio session
                [self activateAudioSession];
                // Notify Qt/C++ code that audio can resume (equivalent to Android audio focus gained)
                ios_handleAudioSessionInterruption(1); // 1 = interruption ended, should resume
            } else {
                ios_handleAudioSessionInterruption(2); // 2 = interruption ended, should not resume
            }
            break;
        }
    }
}

- (void)beginBackgroundTask {
    if (self.backgroundTask != UIBackgroundTaskInvalid) {
        return; // Already have a background task
    }
    
    self.backgroundTask = [[UIApplication sharedApplication]
                          beginBackgroundTaskWithExpirationHandler:^{
                              NSLog(@"IOSAudioManager: Background task expired, ending task");
                              [self endBackgroundTask];
                          }];
    
    if (self.backgroundTask != UIBackgroundTaskInvalid) {
        NSLog(@"IOSAudioManager: Background task started for VoIP");
    } else {
        NSLog(@"IOSAudioManager: Failed to start background task");
    }
}

- (void)endBackgroundTask {
    if (self.backgroundTask != UIBackgroundTaskInvalid) {
        [[UIApplication sharedApplication] endBackgroundTask:self.backgroundTask];
        self.backgroundTask = UIBackgroundTaskInvalid;
        NSLog(@"IOSAudioManager: Background task ended");
    }
}

- (BOOL)isBackgroundTaskActive {
    return self.backgroundTask != UIBackgroundTaskInvalid;
}

- (void)appDidEnterBackground:(NSNotification *)notification {
    NSLog(@"IOSAudioManager: App entered background, starting background task");
    [self beginBackgroundTask];
    
    // We'll restore it when returning to foreground if needed
}

- (void)appWillEnterForeground:(NSNotification *)notification {
    NSLog(@"IOSAudioManager: App entering foreground, ending background task");
    [self endBackgroundTask];
    
    // Restore screen wake lock if it was active before backgrounding
    if (self.screenWakeLockActive) {
        NSLog(@"IOSAudioManager: Restoring screen wake lock after returning from background");
        dispatch_async(dispatch_get_main_queue(), ^{
            [[UIApplication sharedApplication] setIdleTimerDisabled:YES];
        });
    }
}

#pragma mark - Screen Wake Lock (equivalent to Android PowerManager.WakeLock)

- (void)acquireScreenWakeLock {
    if (self.screenWakeLockActive) {
        NSLog(@"IOSAudioManager: Screen wake lock already active");
        return;
    }
    
    dispatch_async(dispatch_get_main_queue(), ^{
        // Disable automatic screen lock (equivalent to Android PARTIAL_WAKE_LOCK)
        // This prevents the screen from going to sleep during voice transmission
        BOOL wasDisabled = [UIApplication sharedApplication].idleTimerDisabled;
        [[UIApplication sharedApplication] setIdleTimerDisabled:YES];
        self.screenWakeLockActive = YES;
        NSLog(@"IOSAudioManager: Screen wake lock acquired (idle timer: %@ → YES)", wasDisabled ? @"YES" : @"NO");
    });
}

- (void)releaseScreenWakeLock {
    if (!self.screenWakeLockActive) {
        NSLog(@"IOSAudioManager: Screen wake lock already inactive");
        return;
    }
    
    dispatch_async(dispatch_get_main_queue(), ^{
        // Re-enable automatic screen lock
        BOOL wasDisabled = [UIApplication sharedApplication].idleTimerDisabled;
        [[UIApplication sharedApplication] setIdleTimerDisabled:NO];
        self.screenWakeLockActive = NO;
        NSLog(@"IOSAudioManager: Screen wake lock released (idle timer: %@ → NO)", wasDisabled ? @"YES" : @"NO");
    });
}

- (BOOL)isScreenWakeLockActive {
    return self.screenWakeLockActive;
}

@end

#pragma mark - C Interface for Qt Integration

void ios_configureVoIPAudioSession(void) {
    [[IOSAudioManager sharedInstance] configureVoIPAudioSession];
}

void ios_activateAudioSession(void) {
    [[IOSAudioManager sharedInstance] activateAudioSession];
}

void ios_deactivateAudioSession(void) {
    [[IOSAudioManager sharedInstance] deactivateAudioSession];
}

void ios_handleAudioSessionInterruption(int interruptionType) {
    // This function is called FROM Objective-C TO C++
    // Forward to the C++ handler
    handleIOSAudioInterruption(interruptionType);
}

void ios_beginBackgroundTask(void) {
    [[IOSAudioManager sharedInstance] beginBackgroundTask];
}

void ios_endBackgroundTask(void) {
    [[IOSAudioManager sharedInstance] endBackgroundTask];
}

int ios_isBackgroundTaskActive(void) {
    return [[IOSAudioManager sharedInstance] isBackgroundTaskActive] ? 1 : 0;
}

double ios_getActualSampleRate(void) {
    return [[[IOSAudioManager sharedInstance] audioSession] sampleRate];
}

double ios_getActualBufferDuration(void) {
    return [[[IOSAudioManager sharedInstance] audioSession] IOBufferDuration];
}

void ios_acquireScreenWakeLock(void) {
    [[IOSAudioManager sharedInstance] acquireScreenWakeLock];
}

void ios_releaseScreenWakeLock(void) {
    [[IOSAudioManager sharedInstance] releaseScreenWakeLock];
}

int ios_isScreenWakeLockActive(void) {
    return [[IOSAudioManager sharedInstance] isScreenWakeLockActive] ? 1 : 0;
}