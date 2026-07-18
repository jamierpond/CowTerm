#include "Trash.h"

#import <Foundation/Foundation.h>

namespace term
{
bool moveToTrash(const std::string& path, std::string& error)
{
    @autoreleasepool
    {
        auto* nsPath = [NSString stringWithUTF8String:path.c_str()];
        auto* url = [NSURL fileURLWithPath:nsPath];

        NSError* err = nil;
        const auto ok = [[NSFileManager defaultManager] trashItemAtURL:url
                                                      resultingItemURL:nil
                                                                 error:&err];

        if (!ok)
            error = err != nil ? std::string {err.localizedDescription.UTF8String}
                               : std::string {"could not move to Trash"};

        return ok;
    }
}
} // namespace term
