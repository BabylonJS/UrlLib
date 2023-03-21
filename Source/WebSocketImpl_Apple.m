#import <WebSocketImpl_Apple.h>

@interface WebSocket_Impl() <NSURLSessionWebSocketDelegate> {
    NSURLSessionWebSocketTask *webSocketTask API_AVAILABLE(ios(13.0));
    NSURLSession *session;
    // store callbacks here
    void (^open_callback)();
    void (^close_callback)();
    void (^error_callback)();
    void (^message_callback)();
}
@end

@implementation WebSocket_Impl

-(void) open:(NSString *)url on_open:(void (^)(void))on_open on_close:(void (^)(void))on_close on_message:(void (^)(NSString *))on_message on_error:(void (^)(void))on_error {
    session = [NSURLSession sessionWithConfiguration:[NSURLSessionConfiguration defaultSessionConfiguration] delegate:self delegateQueue:[NSOperationQueue mainQueue]];
    open_callback = on_open;
    close_callback = on_close;
    error_callback = on_error;
    message_callback = on_message;
    
    webSocketTask = [session webSocketTaskWithURL:[NSURL URLWithString:url] ];
    [webSocketTask resume];
}

- (void) close {
    [webSocketTask cancel];
    [self invalidateAndCancel];
}

- (void) sendMessage:(NSString *)message {
    [webSocketTask sendMessage: [[NSURLSessionWebSocketMessage alloc] initWithString:message] completionHandler:^(NSError * _Nullable error) {
        if (error) {
            error_callback();
        } else {
            NSLog(@"Sent message: %@", message);
        }
    }];
}

- (void)receiveMessage {
    [webSocketTask receiveMessageWithCompletionHandler:^(NSURLSessionWebSocketMessage * _Nullable message, NSError * _Nullable error) {
        if (error) {
            error_callback();
            
        } else if (message.type == NSURLSessionWebSocketMessageTypeString) {
            message_callback(message.string);
            NSLog(@"Received message: %@", message.string);
        }
        [self receiveMessage];
    }];
}

- (void)URLSession:(NSURLSession *)session webSocketTask:(NSURLSessionWebSocketTask *)webSocketTask didOpenWithProtocol:(NSString *)protocol  API_AVAILABLE(ios(13.0)){
    // call the on open callback
    open_callback();
    // run the loop to receive messages
    [self receiveMessage];
    
}

- (void)URLSession:(NSURLSession *)session webSocketTask:(NSURLSessionWebSocketTask *)webSocketTask didCloseWithCloseCode:(NSInteger)code reason:(NSData *)reason  API_AVAILABLE(ios(13.0)){
    [self invalidateAndCancel];
}

- (void)invalidateAndCancel {
    webSocketTask = nil;
    [session invalidateAndCancel];
    session = nil;
    close_callback();
}

@end
