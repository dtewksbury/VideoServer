/*
Copyright (c) 2012, Broadcom Europe Ltd
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// Video Server, based on the Video deocode demo using OpenMAX IL
// through the ilcient helper library
//
// This source was modified by Daryl Tewksbury for the purpose of
// feeding video to LED video walls.
//
// A directory is scanned for files. The files must be raw h.264 files.
// The video decoder and display tunnels are not torn down, so all the
// files must have the same video dimentions as the first file played
// sets the dimensions and framerate, Although if the file is a different
// framerate, it will be played at the currect framerate setting.
// The files will be played sequentially, and in alphabetical order.
// There is a seamless transition from one file to the next.
// If no files are available, a holding video will be looped.
// Once all files have played, it will loop back to the first.
//
// Scheduling can be performed by placing date and time information
// within the filename of the file, this is parsed evrytime a file has
// finished playing. Files can be added, renamed, deleted, moved during
// operation. This is handy if the video directory has been setup as an
// AFP or SMB share so remote management of the playlist is easily done.
//
// Scheduling format:
//
// Using MyVideo.h264 as an example,
//
// MyVideo.h264                         Will always be played
// MyVideo_1600-1630.h264               Will play between 16:00 and 16:30 (30 minutes)
// MyVideo_1630-1600.h264               Will play between 16:30 and 16:00 (23 hours 30 minutes)
// MyVideo_01032015.h264                Will not play after 01 Mar 2015
// MyVideo_1800-01032015.h264           Will not play after 18:00 on 01 Mar 2015
// MyVideo_1800-1900-01032015.h264      Will play between 18:00 - 19:00, but not after 19:00 on 01 Mar 2015
// MyVideo_1800-0200-01032015.h264      Will play between 18:00 - 02:00, but not after 02:00 on 02 Mar 2015
//
// MyVideo_0010110_.h264                Will play on TUE, THU, and FRI (7 character binary string smtwtfs)
//
// The seperators can be any non numeric character, and as many as you want.
// There must be at least one non numeric character after the last date or time.
// so, MyVideo.h264.01032015 will not work!
// The values can be placed at the start of the filename, if you wish.
// You can control the play order with numbers at the start of the filename, they are sorted aphabetically.
//
// Like:
//
// 001-1600-1630-Advertisment.h264
// 002-SmallDemo.h264
// 003-02032015-SmallAd.h264
// 004-1600-1630-02042015-NewMenuItems.h264
// 005-1900-0230-MondaysSpecials.h264
//
// Do not use 4 or 8 number strings that can be interpreted as dates or times for controlling play order.
// 1,2,3,5,6,9 etc, are OK.
//
// Creating the video files.
// The video files must be raw h.264 files with no audio.
// Use ffmpeg to do this like: ffmpeg -i MyVideo.mov MyVideo.h264
// This will strip the stream out of the container.


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>

#include "bcm_host.h"
#include "ilclient.h"

#define MAXDIRENTRIES   256
#define MAXFILENAME     256

char    dirList[MAXDIRENTRIES][MAXFILENAME];
int     currEntry=0;
int     countEntries=0;
int     newFileList=0;
char    tempString[1024];
int     waitVideo=0;
int     quitServer=0;
FILE    *waitVideoHandle=NULL;
FILE    *in=NULL;
struct  timespec time1;


void clrScr()
{
    printf("\033[2J");
}

void cursorXY(int x, int y)
{
    printf("\033[%i;%iH",x,y);
}

void signalHandler( int signum )
{
    quitServer=1;
    printf("Signal: %i\n",signum);
    exit(0);
    
}

// Returns 1 if file can be played
int CheckScheduleTimes(char *str)
{
    time_t t = time(NULL);
    time_t ts,te;
    struct tm tm = *localtime(&t);
    struct tm t1,t2;
    int time_ok=1,date_ok=1,day_ok=1;
    int have_time1=0,have_time2=0,have_date1=0;
    int have_Day=0;
    int length=strlen(str);
    int x;
    int tmp[100];
    int tmppos=0;
    int num4=0,num8=0;
    memset(&t1,0,sizeof(tm));
    memset(&t2,0,sizeof(tm));
    // Extract time and date values
    for(x=0; x<length; x++)
    {
        if(str[x]>='0' && str[x]<='9')
        {
            if(tmppos<100)
            {
                tmp[tmppos++]=str[x]-'0';
            }
        }
        else
        {
            if(tmppos==4)
            {
                num4++;
                if(num4==1)
                {
                    t1.tm_hour=tmp[0]*10+tmp[1];
                    t1.tm_min=tmp[2]*10+tmp[3];
                    have_time1=1;
                }
                else if(num4==2)
                {
                    t2.tm_hour=tmp[0]*10+tmp[1];
                    t2.tm_min=tmp[2]*10+tmp[3];
                    have_time2=1;
                }
            }
            if(tmppos==7)
            {
                int shift=1,x;
                for(x=0; x<7; x++)
                {
                    if(tmp[x])
                        have_Day|=shift;
                    shift<<=1;
                }
            }
            if(tmppos==8)
            {
                num8++;
                if(num8==1)
                {
                    t1.tm_mday=tmp[0]*10+tmp[1];
                    t1.tm_mon=tmp[2]*10+tmp[3]-1;
                    t1.tm_year=(tmp[4]*1000+tmp[5]*100+tmp[6]*10+tmp[7])-1900;
                    t2.tm_mday=tmp[0]*10+tmp[1];
                    t2.tm_mon=tmp[2]*10+tmp[3]-1;
                    t2.tm_year=(tmp[4]*1000+tmp[5]*100+tmp[6]*10+tmp[7])-1900;
                    have_date1=1;
                }
            }
            tmppos=0;
        }
    }
    
  
    
    if(have_date1)
    {
        double diff;
        ts=mktime(&t1);
        te=mktime(&t2);
        if(te < ts) te+=60*60*24;
        if(have_time2)
        {
            if((diff=difftime(te,t)) < 0) date_ok=0;
        }
        else
        {
            if((diff=difftime(ts,t)) < 0) date_ok=0;
        }
        if(have_time2)
            printf("Expire date %02i-%02i-%04i %02i:%02i, Now %02i-%02i-%04i %02i:%02i diff=%0.0f OK=%s\033[K\n",t2.tm_mday,t2.tm_mon+1,t2.tm_year+1900,t2.tm_hour,t2.tm_min,tm.tm_mday,tm.tm_mon+1,tm.tm_year+1900,tm.tm_hour,tm.tm_min,diff,date_ok?"YES":"NO");
        else
            printf("Expire date %02i-%02i-%04i %02i:%02i, Now %02i-%02i-%04i %02i:%02i diff=%0.0f OK=%s\033[K\n",t1.tm_mday,t1.tm_mon+1,t1.tm_year+1900,t1.tm_hour,t1.tm_min,tm.tm_mday,tm.tm_mon+1,tm.tm_year+1900,tm.tm_hour,tm.tm_min,diff,date_ok?"YES":"NO");
    }
    
    if(have_time2)
    {
        // Check time window
        double diff1,diff2;
        if(have_time1 && have_time2)
        {
            t1.tm_mday=tm.tm_mday; t1.tm_mon=tm.tm_mon; t1.tm_year=tm.tm_year;
            t2.tm_mday=tm.tm_mday; t2.tm_mon=tm.tm_mon; t2.tm_year=tm.tm_year;
            ts=mktime(&t1);
            te=mktime(&t2);
            if(te < ts) te+=60*60*24;
            if((diff1=difftime(t,ts)) < 0) time_ok=0;
            if((diff2=difftime(t,te)) > 0) time_ok=0;
        }
        printf("Start %02i:%02i, Finish %02i:%02i, Now %02i:%02i tn-ts=%0.0f tn-te=%0.0f OK=%s\033[K\n",t1.tm_hour,t1.tm_min,t2.tm_hour,t2.tm_min,tm.tm_hour,tm.tm_min,diff1,diff2,time_ok?"YES":"NO");
    }
    
    if(have_Day)
    {
        char tmpstr[64];
        char *days[]={"SUN","MON","TUE","WED","THU","FRI","SAT"};
        int first=0;
        tmpstr[0]=0;
        for(x=0; x<7; x++)
        {
            if((1<<x) & have_Day)
            {
                if(first)
                    strcat(tmpstr,", ");
                strcat(tmpstr,days[x]);
                first=1;
            }
        }
        if(!((1<<tm.tm_wday) & have_Day)) day_ok=0;
        printf("Day filter: %s OK=%s\033[K\n",tmpstr,day_ok?"YES":"NO");
    }

    return (time_ok && date_ok && day_ok);
}

struct timespec diff(struct timespec start, struct timespec end)
{
	struct timespec temp;
	if ((end.tv_nsec-start.tv_nsec)<0) {
		temp.tv_sec = end.tv_sec-start.tv_sec-1;
		temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
	} else {
		temp.tv_sec = end.tv_sec-start.tv_sec;
		temp.tv_nsec = end.tv_nsec-start.tv_nsec;
	}
	return temp;
}

void profileStart()
{
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time1);
}

void profileEnd()
{
    struct timespec tm;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tm);
    time1=diff(time1,tm);
    if(time1.tv_sec)
        printf("%i.%09li s\n",(int)(time1.tv_sec),time1.tv_nsec);
    printf("%li ns\n",time1.tv_nsec);
}

int addToListSorted(char *str)
{
    if(!str || strlen(str)==0) return -1;
    int x,done=0;
    if(countEntries >= MAXDIRENTRIES) return -2;
    printf("Adding: %s\033[K\n",str);
    if(CheckScheduleTimes(str))
    {
        for(x=0; x<countEntries; x++)
        {
            if(strcmp(str,dirList[x])<0)
            {
                int y;
                for(y=countEntries; y>x; y--)
                    strcpy(dirList[y],dirList[y-1]);
                strcpy(dirList[x],str);
                done=1;
                break;
            }
            else if(strcmp(str,dirList[x])==0)
            {
                printf("Not Added to: %02i (duplicate)\033[K\n",x+1);
                return x;
            }
        }
        if(!done)
        {
            strcpy(dirList[countEntries],str);
            countEntries++;
            printf("Added to: %02i\033[K\n",countEntries);
            return countEntries-1;
        }
        countEntries++;
        printf("Inserted at: %02i\033[K\n",x+1);
    }
    else
    {
        printf("Not added due to time restriction\033[K\n");
    }
    return x;
}

int scanDirectories(char *directory, int type)
{
    DIR           *d;
    struct dirent *dir;
    d = opendir(directory);
    countEntries=0;
    cursorXY(0,0);
    if (d)
    {
        while ((dir = readdir(d)) != NULL)
        {
            if(dir->d_name[0]!='.')
            {
                sprintf(tempString,"%s/%s",directory,dir->d_name);
                if(strlen(dir->d_name)==1 && type==1 && dir->d_type==DT_DIR)
                {
                    if(dir->d_name[0]=='x')
                    {
                        printf(" Exit found and removed\033[K\n");
                    }
                    if(dir->d_name[0]=='s')
                    {
                        printf(" Stop found and removed\033[K\n");
                    }
                    if(dir->d_name[0]=='r')
                    {
                        printf(" Resume found and removed\033[K\n");
                    }
                    rmdir(tempString);
                }
                if(dir->d_type==DT_REG)
                {
                    int r=addToListSorted(tempString);
                    if(r<0)
                    {
                        if(r == -1) printf("Add to list failed (NULL or Empty string)\033[K\n");
                        if(r == -2) printf("Add to list failed (Already full)\033[K\n");
                    }
                }
            }
        }
        closedir(d);
    }
    int x;
    for(x=0; x<countEntries; x++)
        printf("%02i: %s\033[K\n",x+1,dirList[x]);
    return 0;
}

int checkForCommands(char *directory)
{
    DIR           *d;
    struct dirent *dir;
    d = opendir(directory);
    if (d)
    {
        while ((dir = readdir(d)) != NULL)
        {
            if(dir->d_name[0]!='.')
            {
                sprintf(tempString,"%s/%s",directory,dir->d_name);
                //printf("%s", tempString);
                if(strlen(dir->d_name)==1)
                {
                    if(dir->d_name[0]=='x') // Exit
                    {
                        printf(" Exit Signalled\033[K/n");
                        rmdir(tempString);
                        return 1;
                    }
                    if(dir->d_name[0]=='s') // Stop
                    {
                        printf(" Stop Signalled\033[K/n");
                        rmdir(tempString);
                        return 2;
                    }
                    if(dir->d_name[0]=='r') // resume
                    {
                        printf(" Resume Signalled\033[K/n");
                        rmdir(tempString);
                        return 3;
                    }
                }
            }
        }
        closedir(d);
    }
    return 0;
}


int verifyFilesExistInList(void)
{
    int x;
    for(x=0; x<countEntries; x++)
    {
        if(access(dirList[currEntry],F_OK))
            return 1;
    }
    return 0;
}

int openFirstOrNextVideoFile(char *directory)
{
    if(in) fclose(in); in=NULL;
    scanDirectories(directory,0);
    
    if(!countEntries)
    {
        return 1;
    }
    
    if(countEntries==1)
    {
        currEntry=0;
        if((in = fopen(dirList[currEntry], "rb")) == NULL)
        {
            printf("Error opening: %s\033[K\n",dirList[currEntry]);
            return 1;
        }
        else
        {
            printf("Success opening: %s\033[K\n",dirList[currEntry]);
            return 0;
        }
    }
    else
    {
        if((in = fopen(dirList[currEntry], "rb")) == NULL)
        {
            printf("Error opening: %s\033[K\n",dirList[currEntry]);
            return 1;
        }
        printf("Success opening: %s\033[K\n",dirList[currEntry]);
        currEntry=(currEntry+1)%countEntries;
    }
    return 0;
}

static int video_decode_test(char *directory)
{
   OMX_VIDEO_PARAM_PORTFORMATTYPE format;
   OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;
   COMPONENT_T *video_decode = NULL, *video_scheduler = NULL, *video_render = NULL, *clock = NULL;
   COMPONENT_T *list[5];
   TUNNEL_T tunnel[4];
   ILCLIENT_T *client;
   //FILE *in=NULL;
   int status = 0;
    int filePos=0;
   unsigned int data_len = 0;

   memset(list, 0, sizeof(list));
   memset(tunnel, 0, sizeof(tunnel));
    if((waitVideoHandle=fopen("/opt/vc/src/hello_pi/video_server/WaitingForVideoLoop.h264", "rb"))==NULL)
    {
        printf("Error Opening file (WaitingForVideoLoop.h264)\033[K\n");
        waitVideoHandle=NULL;
    }
    else
    {
        printf("File (WaitingForVideoLoop.h264), opened successfully\033[K\n");
    }
    scanDirectories(directory,1);
    openFirstOrNextVideoFile(directory);

   if((client = ilclient_init()) == NULL)
   {
      if(in)
          fclose(in);
      return -3;
   }

   if(OMX_Init() != OMX_ErrorNone)
   {
      ilclient_destroy(client);
      if(in)
          fclose(in);
      return -4;
   }

   // create video_decode
   if(ilclient_create_component(client, &video_decode, "video_decode", ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS) != 0)
      status = -14;
   list[0] = video_decode;

   // create video_render
   if(status == 0 && ilclient_create_component(client, &video_render, "video_render", ILCLIENT_DISABLE_ALL_PORTS) != 0)
      status = -14;
   list[1] = video_render;

   // create clock
   if(status == 0 && ilclient_create_component(client, &clock, "clock", ILCLIENT_DISABLE_ALL_PORTS) != 0)
      status = -14;
   list[2] = clock;

   memset(&cstate, 0, sizeof(cstate));
   cstate.nSize = sizeof(cstate);
   cstate.nVersion.nVersion = OMX_VERSION;
   cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
   cstate.nWaitMask = 1;
   if(clock != NULL && OMX_SetParameter(ILC_GET_HANDLE(clock), OMX_IndexConfigTimeClockState, &cstate) != OMX_ErrorNone)
      status = -13;

   // create video_scheduler
   if(status == 0 && ilclient_create_component(client, &video_scheduler, "video_scheduler", ILCLIENT_DISABLE_ALL_PORTS) != 0)
      status = -14;
   list[3] = video_scheduler;

   set_tunnel(tunnel, video_decode, 131, video_scheduler, 10);
   set_tunnel(tunnel+1, video_scheduler, 11, video_render, 90);
   set_tunnel(tunnel+2, clock, 80, video_scheduler, 12);

   // setup clock tunnel first
   if(status == 0 && ilclient_setup_tunnel(tunnel+2, 0, 0) != 0)
      status = -15;
   else
      ilclient_change_component_state(clock, OMX_StateExecuting);

   if(status == 0)
      ilclient_change_component_state(video_decode, OMX_StateIdle);

   memset(&format, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
   format.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
   format.nVersion.nVersion = OMX_VERSION;
   format.nPortIndex = 130;
   format.eCompressionFormat = OMX_VIDEO_CodingAVC;
    


   if(status == 0 &&
      OMX_SetParameter(ILC_GET_HANDLE(video_decode), OMX_IndexParamVideoPortFormat, &format) == OMX_ErrorNone &&
      ilclient_enable_port_buffers(video_decode, 130, NULL, NULL, NULL) == 0)
   {
      OMX_BUFFERHEADERTYPE *buf;
      int port_settings_changed = 0;
      int first_packet = 1;
       OMX_SetParameter(ILC_GET_HANDLE(video_render), OMX_IndexParamVideoPortFormat, &format);
      ilclient_change_component_state(video_decode, OMX_StateExecuting);

        while((buf = ilclient_get_input_buffer(video_decode, 130, 1)) != NULL)
        {
            // feed data and wait until we get port settings changed
            unsigned char *dest = buf->pBuffer;
            int r;
            //profileStart();
          
          
            int command=checkForCommands(directory);
            if(command==1) quitServer=1;
            if(command==2)
            {
                if(in)
                    fclose(in);
                in=NULL;
                filePos=0;
                waitVideo=1;
                countEntries=0;
            }
            if(command==3)
            {
                waitVideo=openFirstOrNextVideoFile(directory);
                first_packet=1;
                filePos=0;
            }
            
            if((!countEntries || waitVideo) && waitVideoHandle)
            {
                data_len += (r=fread(dest, 1, buf->nAllocLen-data_len, waitVideoHandle));
            }
            else
            {
                if(in)
                    data_len += (r=fread(dest, 1, buf->nAllocLen-data_len, in));
            }
            
              //profileEnd();
            filePos+=r;
            //printf("%i\n",filePos);
            if(port_settings_changed == 0 &&
            ((data_len > 0 && ilclient_remove_event(video_decode, OMX_EventPortSettingsChanged, 131, 0, 0, 1) == 0) ||
             (data_len == 0 && ilclient_wait_for_event(video_decode, OMX_EventPortSettingsChanged, 131, 0, 0, 1,
                                                       ILCLIENT_EVENT_ERROR | ILCLIENT_PARAMETER_CHANGED, 10000) == 0)))
            {
                port_settings_changed = 1;

                if(ilclient_setup_tunnel(tunnel, 0, 0) != 0)
                {
                    status = -7;
                    break;
                }

                ilclient_change_component_state(video_scheduler, OMX_StateExecuting);

                // now setup tunnel to video_render
                if(ilclient_setup_tunnel(tunnel+1, 0, 1000) != 0)
                {
                    status = -12;
                    break;
                }

                ilclient_change_component_state(video_render, OMX_StateExecuting);
            }

            if(!data_len)
            {

                if(waitVideoHandle)
                {
                    fseek(waitVideoHandle,0,SEEK_SET);
                    filePos=0;
                }
                
                openFirstOrNextVideoFile(directory);
                if(in) fseek(in,0,SEEK_SET);
                
                if(quitServer) break;
            }
         
            buf->nFilledLen = data_len;
            data_len = 0;

            buf->nOffset = 0;
            if(first_packet)
            {
                buf->nFlags = OMX_BUFFERFLAG_STARTTIME;
                first_packet = 0;
            }
            else
                buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;

            if(OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_decode), buf) != OMX_ErrorNone)
            {
                status = -6;
                break;
            }
            //if(command==1) break; // Exit!!!!
        }

        buf->nFilledLen = 0;
       
        buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN | OMX_BUFFERFLAG_EOS;

        if(OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_decode), buf) != OMX_ErrorNone)
            status = -20;

        // wait for EOS from render
        ilclient_wait_for_event(video_render, OMX_EventBufferFlag, 90, 0, OMX_BUFFERFLAG_EOS, 0,
                              ILCLIENT_BUFFER_FLAG_EOS, 10000);

        // need to flush the renderer to allow video_decode to disable its input port
        ilclient_flush_tunnels(tunnel, 0);

        ilclient_disable_port_buffers(video_decode, 130, NULL, NULL, NULL);
    }

    if(in) fclose(in);

    ilclient_disable_tunnel(tunnel);
    ilclient_disable_tunnel(tunnel+1);
    ilclient_disable_tunnel(tunnel+2);
    ilclient_teardown_tunnels(tunnel);

    ilclient_state_transition(list, OMX_StateIdle);
    ilclient_state_transition(list, OMX_StateLoaded);

    ilclient_cleanup_components(list);

    OMX_Deinit();

    ilclient_destroy(client);
    return status;
}

//int main (int argc, char **argv)
int main()
{
    memset(dirList,0,MAXDIRENTRIES*MAXFILENAME);
    signal(SIGTERM, signalHandler);
    bcm_host_init();
    clrScr();
    return video_decode_test("/home/pi/videos"/*argv[1]*/);
}


