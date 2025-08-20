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

#ifndef IOSAUDIOMANAGER_H
#define IOSAUDIOMANAGER_H

#ifdef __OBJC__
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// C interface for Qt integration
void ios_configureVoIPAudioSession(void);
void ios_activateAudioSession(void);
void ios_deactivateAudioSession(void);
void ios_handleAudioSessionInterruption(int interruptionType);
void ios_beginBackgroundTask(void);
void ios_endBackgroundTask(void);
int ios_isBackgroundTaskActive(void);
double ios_getActualSampleRate(void);
double ios_getActualBufferDuration(void);

// Screen wake lock functions (equivalent to Android PowerManager.WakeLock)
void ios_acquireScreenWakeLock(void);
void ios_releaseScreenWakeLock(void);
int ios_isScreenWakeLockActive(void);

#ifdef __cplusplus
}
#endif

#ifdef __OBJC__
// Objective-C interface
@interface IOSAudioManager : NSObject

+ (instancetype)sharedInstance;
- (void)configureVoIPAudioSession;
- (void)activateAudioSession;
- (void)deactivateAudioSession;
- (void)handleAudioSessionInterruption:(NSNotification *)notification;
- (void)beginBackgroundTask;
- (void)endBackgroundTask;
- (BOOL)isBackgroundTaskActive;

// Screen wake lock methods (equivalent to Android PowerManager.WakeLock)
- (void)acquireScreenWakeLock;
- (void)releaseScreenWakeLock;
- (BOOL)isScreenWakeLockActive;

@property (nonatomic, assign) UIBackgroundTaskIdentifier backgroundTask;
@property (nonatomic, strong) AVAudioSession *audioSession;
@property (nonatomic, assign) BOOL screenWakeLockActive;

@end
#endif

#endif // IOSAUDIOMANAGER_H