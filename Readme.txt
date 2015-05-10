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
// You can control the play order with numbers at the start of the filename, they are sorted alphabetically.
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