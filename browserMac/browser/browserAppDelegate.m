#include "env.h"
#include "sharelib.h"
#include "history.h"
#include <stdio.h>
#include <locale.h>
#include	<sys/types.h>	/* basic system data types */
#include	<sys/socket.h>	/* basic socket definitions */
#include	<sys/time.h>	/* timeval{} for select() */
#include	<time.h>		/* timespec{} for pselect() */
#include	<netinet/in.h>	/* sockaddr_in{} and other Internet defns */
#include	<arpa/inet.h>	/* inet(3) functions */
#include	<errno.h>
#include	<fcntl.h>		/* for nonblocking */
#include	<netdb.h>
#include	<signal.h>
#include	<sys/stat.h>	/* for S_xxx file mode constants */
#include	<sys/uio.h>		/* for iovec{} and readv/writev */
#include	<unistd.h>
#include	<sys/wait.h>
#include	<sys/un.h>		/* for Unix domain sockets */

static BOOL connect_unix_socket(int *psock) {
	int	sockfd;
	sockfd = socket(AF_LOCAL, SOCK_STREAM, 0);
	if(sockfd<0) {
		printf("unix domain socket error");
		return 0;
	}else{
		struct sockaddr_un	servaddr;
		bzero(&servaddr, sizeof(servaddr));
		servaddr.sun_family = AF_LOCAL;
		strcpy(servaddr.sun_path, UNIXSTR_PATH);
		if(connect(sockfd, (SA *) &servaddr, sizeof(servaddr))!=0 ) {
			printf("connect error");
			return 0;
		}else{
			*psock = sockfd;
			return 1;
		}
	}
}

#import "browserAppDelegate.h"

@implementation browserAppDelegate

@synthesize window;
@synthesize webView;

-(id) init {
    self = [super init];
	if (self) {
		dir = [[NSString alloc] initWithString:@""];
	}
	return self;
}

- (void) dealloc {
	self.dir = nil;
	[super dealloc];
}

- (NSString *)dir {
	NSLog(@"%@ received %@", self, NSStringFromSelector(_cmd));
    return [[dir retain] autorelease];
}

- (void)setDir:(NSString *)value {
	NSLog(@"%@ received %@", self, NSStringFromSelector(_cmd));
    if (dir != value) {
        [dir release];
        dir = [value copy];
    }
}

- (void)awakeFromNib {
    setlocale(LC_ALL, "");
	NSString *htmlPath = @"/Users/ylt/Documents/gigaso/browser/web/search.htm";
	[[webView mainFrame] loadRequest:[NSURLRequest requestWithURL:[NSURL fileURLWithPath:htmlPath]]];
    [window setDelegate:self];
    [webView setUIDelegate: self];
    [webView setGroupName:@"Gigaso"];
    [webView setFrameLoadDelegate: self];
	[webView setResourceLoadDelegate: self];
    order=0;
    file_type=0;
    caze=false;
    offline=false;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender{
    return TRUE;
}

- (NSSize)windowWillResize:(NSWindow *)sender toSize:(NSSize)frameSize{
    NSLog(@"will resize ");
    return frameSize;
}
- (BOOL)windowShouldZoom:(NSWindow *)window toFrame:(NSRect)newFrame{
    NSLog(@"should zoom ");
    [webView setFrame:newFrame];
    return true;
}


- (void)webView:(WebView *)webView windowScriptObjectAvailable:(WebScriptObject *)windowScriptObject{
    [windowScriptObject setValue:self forKey:@"plugin"];
    [windowScriptObject evaluateWebScript: @"cef = {};cef.plugin=plugin;"];
    connect_unix_socket(&sockfd);
}

- (void)webView:(WebView *)sender didFinishLoadForFrame:(WebFrame *)frame{
    return;
    id win = [webView windowScriptObject];
    {
        NSArray *args = [NSArray arrayWithObjects:
                         @"search.exe \"/User/ylt\"",nil];
        [win callWebScriptMethod:@"init_dir" withArguments:args];
    }
    return;
    {
        [win evaluateWebScript: @"init_dir('/User/ylt')"];
    }
}

- (void)webView:(WebView *)sender runJavaScriptAlertPanelWithMessage:(NSString *)message {
	NSLog(@"%@", message);
}
- (WebView *)webView:(WebView *)sender createWebViewWithRequest:(NSURLRequest *)request{
    NSLog(@"%@", request);//为什么request is null?
    //[[webView mainFrame] loadRequest: request];
    NSLog(@"%@", [[[request URL] absoluteString ] UTF8String]);
    system([[[request URL] absoluteString ] UTF8String] );
    return webView;
}


+ (BOOL)isSelectorExcludedFromWebScript:(SEL)selector {
    return NO;
}

+ (BOOL)isKeyExcludedFromWebScript:(const char *)property {
    return NO;
}

+ (NSString *)webScriptNameForKey:(const char *)name{
    if (strcmp(name, "dir")==0) {
		return @"dire";
	} else {
		return nil;
	}
}

+ (NSString *) webScriptNameForSelector:(SEL)sel {
    if (sel == @selector(search:)) {
		return @"search";
    } else if (sel == @selector(stat:)) {
		return @"stat";
    } else if (sel == @selector(his_del:)) {
		return @"his_del";
    } else if (sel == @selector(his_pin:)) {
		return @"hispin";
    } else if (sel == @selector(his_unpin:)) {
		return @"his_unpin";
	} else {
		return nil;
	}
}

static int MAX_ROW = 30;

- (NSString*) query: (NSString*) query row: (int) row{
    NSLog(query);
    SearchRequest req;
	SearchResponse resp;
	memset(&req,0,sizeof(SearchRequest));
	req.from = 0;
	req.rows = row;
	req.env.order = order;
	req.env.case_sensitive = caze;
	req.env.offline = offline? 1:0;
	req.env.file_type = file_type;
	req.env.path_len = [dir length];
	if(req.env.path_len>0){
        const char * dutf8 = [dir UTF8String];
        mbsnrtowcs(req.env.path_name, (const char **)&dutf8,  strlen(dutf8), MAX_PATH, NULL);
    }
	if([query length]==0) return @"";
    const char * qutf8 = [query UTF8String];
	mbsnrtowcs(req.str, (const char **)&qutf8,  strlen(qutf8), MAX_PATH, NULL);
    if (write(sockfd, &req, sizeof(SearchRequest))<=0) {
        printf("scoket write error");
        return @"error";
    }
    if(read(sockfd, &resp, sizeof(int))>0){
        char buffer[MAX_RESPONSE_LEN];
        DWORD len = resp.len;
        printf("%d,", len);
        memset(buffer,(char)0,MAX_RESPONSE_LEN);
        if(read(sockfd, buffer, len)<=0){
            printf("scoket read error.");
        }
        return [NSString stringWithUTF8String: buffer];
    }else{
        printf("scoket read error");
        return @"error";
    }
}

- (NSString*) search: (NSString*) query{
    return [self query:query row:MAX_ROW];
}

- (NSString*) stat: (NSString*) query{
    return [self query:query row:-1];
}

#if big_endian
#define WCHAR_ENCODING NSUTF32BigEndianStringEncoding
#else
#define WCHAR_ENCODING NSUTF32LittleEndianStringEncoding
#endif


- (NSString*) history{
	wchar_t buffer[VIEW_HISTORY*MAX_PATH];
	int len;
	history_load();
	len = history_to_json(buffer);
    //return [NSString stringWithCharacters:buffer length:len];
    return [[NSString alloc] initWithBytes:buffer length:(len * sizeof(wchar_t)) encoding:WCHAR_ENCODING];
}

- (BOOL) his_del: (int) index{
    history_delete(index);
    return history_save();
}
- (BOOL) his_pin: (int) index{
    history_pin(index);
    return history_save(); 
}
- (BOOL) his_unpin: (int) index{
    history_unpin(index);
    return history_save();
}

@end