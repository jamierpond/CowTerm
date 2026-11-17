#include "ScreenAwake.h"

#import <UIKit/UIKit.h>

namespace cowterm::mobile
{
void setScreenKeptAwake(bool isKeptAwake)
{
    [UIApplication sharedApplication].idleTimerDisabled = isKeptAwake ? YES : NO;
}
} // namespace cowterm::mobile
