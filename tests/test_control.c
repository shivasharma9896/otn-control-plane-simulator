#include "control_channel.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
int main(void){
 int sv[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,sv)==0);for(int i=0;i<2;i++){int f=fcntl(sv[i],F_GETFL,0);assert(fcntl(sv[i],F_SETFL,f|O_NONBLOCK)==0);}
 control_channel_t a,b;assert(control_channel_init(&a,sv[0],2)==0);assert(control_channel_init(&b,sv[1],1)==0);
 const char msg[]="state";assert(control_queue(&a,CONTROL_STATE,msg,sizeof(msg))==0);assert(control_flush(&a)==0);
 control_header_t h;uint8_t p[64];int ready=0;assert(control_read(&b,&h,p,sizeof(p),&ready)==0);assert(ready&&h.type==CONTROL_STATE&&h.length==sizeof(msg)&&memcmp(p,msg,sizeof(msg))==0);
 uint8_t frame[CONTROL_HEADER_SIZE+CONTROL_MAX_PAYLOAD];size_t frame_len=0;assert(control_encode(CONTROL_ACK,msg,sizeof(msg),frame,sizeof(frame),&frame_len)==0);
 assert(send(sv[0],frame,5,0)==5);ready=0;assert(control_read(&b,&h,p,sizeof(p),&ready)==0&&!ready);
 assert(send(sv[0],frame+5,frame_len-5,0)==(ssize_t)(frame_len-5));assert(control_read(&b,&h,p,sizeof(p),&ready)==0&&ready&&h.type==CONTROL_ACK);
 uint32_t generation = 7U;
 assert(control_queue(&a,CONTROL_STATE,&generation,sizeof(generation))==0);
 assert(control_flush(&a)==0);
 ready=0; assert(control_read(&b,&h,p,sizeof(p),&ready)==0&&ready&&h.type==CONTROL_STATE);
 uint32_t received_generation=0; memcpy(&received_generation,p,sizeof(received_generation));
 assert(received_generation==generation);
 frame[12] ^= 0x01;
 assert(send(sv[0],frame,frame_len,0)==(ssize_t)frame_len);
 assert(control_read(&b,&h,p,sizeof(p),&ready)<0);
 assert(control_encode(CONTROL_ACK,msg,CONTROL_MAX_PAYLOAD + 1U,frame,sizeof(frame),&frame_len)<0);
 close(sv[0]);close(sv[1]);puts("control tests passed");return 0;
}
