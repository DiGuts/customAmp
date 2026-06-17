
#include <TargetConditionals.h>
#if TARGET_OS_IOS == 1
  #import <UIKit/UIKit.h>
#else
  #import <Cocoa/Cocoa.h>
#endif

#define IPLUG_AUVIEWCONTROLLER IPlugAUViewController_vAmpForge
#define IPLUG_AUAUDIOUNIT IPlugAUAudioUnit_vAmpForge
#import <AmpForgeAU/IPlugAUAudioUnit.h>
#import <AmpForgeAU/IPlugAUViewController.h>

//! Project version number for AmpForgeAU.
FOUNDATION_EXPORT double AmpForgeAUVersionNumber;

//! Project version string for AmpForgeAU.
FOUNDATION_EXPORT const unsigned char AmpForgeAUVersionString[];

@class IPlugAUViewController_vAmpForge;

