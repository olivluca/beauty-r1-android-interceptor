/*************************************************************************

  Translates Beauty-R1 clicker mouse movements into keypresses

    Copyright (C) 2023  Luca Olivetti

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.

**************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <string.h>
#include <stdbool.h>
#include <sys/time.h>
#include <sys/inotify.h>
#include <signal.h>
#include "keycodes.h"

int button=0;
int x=1904;
int y=1904;

#define UP 1
#define DOWN 2
#define LEFT 3
#define RIGHT 4
int direction=0;


bool debug=false;

#define debugln(...) if (debug) printf(__VA_ARGS__)

/*
  Checks if a device in /dev/input is the Beauty-R1.
  If it is, opens it and return the handle
  
*/
int check_beauty(char *devname) {
  int fd;
  char name[256];
  char filename[300];
  
  if (strncmp("event", devname, strlen("event"))!=0) {
    return -1;
  }  
  snprintf(filename, 300, "/dev/input/%s", devname);
  if ((fd=open(filename, O_RDONLY)) > 0) {
    ioctl(fd, EVIOCGNAME(sizeof(name)),name);
    printf("Device %s name %s\n",filename,name);
    if (strcmp("Beauty-R1",name)==0) {
      printf("***FOUND\n");
      ioctl(fd,EVIOCGRAB,1);
      return fd;
    }
    close(fd);
  } else {
            printf("cannot open %s\n",filename);
  }
  return -1;
}


/*
 inotify event, checks if the created file is the Beauty-R1
 
*/
int get_event (int fd) {
    char buffer[sizeof(struct inotify_event) + PATH_MAX+ 1];
    int length, i = 0;
    int result=-1;
 
    length = read( fd, buffer, sizeof(buffer) );  
    if ( length < 0 ) {
        perror( "read" );
    }  
  
        struct inotify_event *event = ( struct inotify_event * ) &buffer;
        if ( event->len ) {
            if ( event->mask & IN_CREATE) {
                if (event->mask & IN_ISDIR) {
                    printf( "The directory %s was Created.\n", event->name );       
                }  else {
                    printf( "The file %s was Created with WD %d\n", event->name, event->wd );       
                    result=check_beauty(event->name);
                }    
            }
        }
    return result;
}


/*
 Opens the /dev/input/eventX device corresponding to the 
 Beauty-R1.
 First checks if the device already exists in the directory,
 if it doesn't uses inotify to wait for its creation.
  
*/
int open_beauty() {

  DIR *dp;
  int fd,wd,result;
  
  struct dirent *ep;
  struct inotify_event ev;
  
  dp = opendir("/dev/input/");
  if (dp != NULL) {
     while ((ep = readdir(dp)) != NULL) {
       if ((fd=check_beauty(ep->d_name)) > 0) {
          closedir(dp);
          return fd;
       }
     }
     closedir(dp);
     //
    fd = inotify_init();
    if ( fd < 0 ) {
        perror( "Couldn't initialize inotify");
    }
    wd = inotify_add_watch(fd, "/dev/input/", IN_CREATE); 
    if (wd == -1) {
        printf("Couldn't add watch to /dev/input/\n");
    } else {
        printf("Watching:: /dev/input/\n");
    }
  
    /* do it forever*/
    result=-1;
    while(result<0) {
        result=get_event(fd); 
    } 
 
    /* Clean up*/
    inotify_rm_watch( fd, wd );
    close( fd );
    return result;
    
  } else {
    printf("cannot open /dev/input\n");
    return -1;
  }
} 


/*
  Forks and sends a keycode using the "input" program
    
*/
static int sendkeycode(int k)
{
    int my_pid;
    char buffer[100];
    snprintf(buffer, sizeof(buffer), "input keyevent %d", k);
    if (0 == (my_pid = fork())) {
            system(buffer);
            exit(0);
    }
    return 0;
}

/*
 Actions for a button
 
*/
enum buttons     {btUp, btDown, btLeft, btRight, btEnter, btPhoto, btLongUp, btLongDown, btLongLeft, btLongRight};
char *btname[] = {"UP", "DOWN", "LEFT", "RIGHT", "ENTER", "PHOTO", "LONGUP", "LONGDOWN", "LONGLEFT", "LONGRIGHT"};
int btcodes[] =  {AKEYCODE_DPAD_UP,         //btUp
                  AKEYCODE_DPAD_DOWN,       //btDown
                  AKEYCODE_DPAD_LEFT,       //btLeft
                  AKEYCODE_DPAD_RIGHT,      //btRight
                  AKEYCODE_ENTER,           //btEnter
                  AKEYCODE_CAMERA,          //btPhoto
                  AKEYCODE_CALL,            //btLongUp
                  AKEYCODE_MUTE,            //btLongDown
                  AKEYCODE_MEDIA_PREVIOUS,  //btLongLeft
                  AKEYCODE_MEDIA_NEXT       //btLongRight
                  };

void dobutton(enum buttons b) {
    printf(":%s\n",btname[b]);
    sendkeycode(btcodes[b]);
}

/*
  New x position received, checks if it's moving right/left 
  
*/
void setx(struct input_event *ev) 
{
  if (button) {
    if (ev->value>x) {
      debugln("right\n");
      direction=LEFT;
    }  
    if (ev->value<x) {
      debugln("left\n");
      direction=RIGHT; 
    }   
  }
  x=ev->value;
  debugln("x now is %d\n",x);
}


/*
 New y position received, checks if it moves up or down
 When x is 2048 it's a long press, report just the first
 repetition (y 784 or 2912).
 
*/ 
void sety(struct input_event *ev)
{
  struct timeval timediff;
  if (button) {
    if (ev->value>y) {
      debugln("down\n");
      direction=UP;
    }
    if (ev->value<y) {
      debugln("up\n");
      direction=DOWN; 
    }   
  }
  y=ev->value;
  debugln("y now is %d\n",y);
  if (x==2048 && button) {
     //to avoid repetitions only use the first event (depending on y)
     if (y==784)
       dobutton(btLongUp);
     if (y==2912)
       dobutton(btLongDown);  
  }
}

/*
 Checks the elapsed time between two timestamps
 
*/
bool lapsed(struct timeval *then, struct timeval *now, long secs, long usecs) {

  struct timeval timediff;
  timersub(now, then, &timediff);
  if (timediff.tv_sec>secs)
    return true;
  if (timediff.tv_sec<secs)
    return false;
  return timediff.tv_usec>=usecs;    
}

/*
 Register button presses and releases
 When pressed, reset the direction.
 when released,check the position:
   1904/1904 is the central button
   1536/608 is the photo button 
   otherwise use the direction to know if it was
   up/down/left/right
 
*/
 
void setbutton(struct input_event *ev) {
  if (ev->value!=button) {
    button=ev->value;
    if (button>0) {
      debugln("button press\n");
      direction=0;
    } else {
      debugln("button release\n");
      if ( x==1904 && y==1904) {
        dobutton(btEnter);
        return;
      } 
      if ( x==1536 && y==608) {
        dobutton(btPhoto);
        return;
      }
      debugln("direction %d\n",direction); 
      if (direction==RIGHT) {
        dobutton(btRight);
        return;
      }
      if (direction==LEFT) {
        dobutton(btLeft);
        return;
      } 
      //when x is 2048 it's a long press, don't report normal up/down
      if (x!=2048 && direction==UP) {
        dobutton(btUp);
        return;
      }
      if (x!=2048 && direction==DOWN) {
        dobutton(btDown);
        return;
      } 
    }
  }
}

/*

 Checks for the long press of the left/right buttons.
 
 These button are reported repeatedly as KEY_VOLUMEDOWN, KEY_VOLUMEUP,
 here we're using the elapsed time to only report the first repetition.
 
*/
 
#define LONGLEFT 0
#define LONGRIGHT 1
void longpress(struct input_event* ev, int index) {
    static struct timeval prevlong[2];
    static bool longdone[2]={false,false};
    static int longkeycodes[2]={KEY_VOLUMEDOWN, KEY_VOLUMEUP};
    static enum buttons longbuttons[2]={btLongLeft, btLongRight};
    //after a different keypress or 1.3 seconds (the repetition is about 1.1 seconds)
    //we can accept a new long press
    if (longdone[index]) {
      if (ev->code!=longkeycodes[index] || lapsed(&prevlong[index],&ev->time,1,300000))
        longdone[index]=false;
    }
    if (ev->code==longkeycodes[index]) {
         if (!longdone[index]) 
           dobutton(longbuttons[index]);
         longdone[index]=true;
         prevlong[index]=ev->time;
           
    }   
}

/*
 Main
*/  

int main(int argc, char **argv)
{
  if (argc>1) {
      if (strcmp(argv[1],"debug")==0) {
          debug=true;
      }
  }
    
  int fd;
  struct input_event ie;
    
  signal(SIGCHLD, SIG_IGN); //avoid zombies

  while (true) {   
    fd=open_beauty();
    if (fd<0)
      exit;
    while(read(fd, &ie, sizeof(struct input_event))>0)
    {
        debugln("ts %10ld.%10ld type %d code %d value %d\n",ie.time.tv_sec,ie.time.tv_usec,ie.type, ie.code, ie.value);
        
        switch(ie.type) {
          case EV_ABS:
            switch(ie.code) {
              case ABS_X:
                setx(&ie);
                break;
              case ABS_Y:
                sety(&ie);
                break;  
            }
            break;
          case EV_KEY:
            if (ie.code==BTN_TOOL_PEN) 
              setbutton(&ie);
            if (ie.value==1) {
              longpress(&ie, LONGLEFT);
              longpress(&ie, LONGRIGHT); 
            }  
            break;  
        }
    }
    close(fd);
  }  
return 0;
}
