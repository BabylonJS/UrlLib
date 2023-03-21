#import <Foundation/Foundation.h>

API_AVAILABLE(ios(13.0))
@interface WebSocket_Impl : NSObject
- (void)open:(NSString *)url on_open:(void (^)(void))on_open on_close:(void (^)(void))on_close on_message:(void (^)(NSString *))on_message on_error:(void (^)(void))on_error API_AVAILABLE(ios(13.0));
- (void)close API_AVAILABLE(ios(13.0));
- (void)sendMessage:(NSString *)message  API_AVAILABLE(ios(13.0));
- (void)receiveMessage API_AVAILABLE(ios(13.0));

@end
