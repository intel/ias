                                 
                                   dma-test
                        =============================

This application shows the capability of DMA buffers of processing data from IPU. 
It first requests GEM buffers from DRM. These buffers are then shared with 
the IPU driver which maps them into its own address space. Once we start 
streaming, the buffers are written by the IPU driver and can immediately 
be seen by DRM as well. Then this application maps the buffers into its own address 
space and finally creates a texture out of them. Except for this last step (creation
of a texture), everything else is done without any CPU involvement. On average,
about 1 % CPU utilization was observed without texture creation. With texture
creation, the CPU utilization jumped up 2-2.5% for 1920x1080@60fps, the same for
camera 720x240@60fps This texture creation by the client allows it to scale, rotate, 
color scale conversion and do anything else that the client wishes using the GPU. Note that 
creation of a texture is only necessary mainly for the color conversion. In Apollo Lake, 
our incoming frames are pixel format YV16 and we need to display pixel format RGB888. 
If on a customer board the incoming frames are already pixel format RGB888, no conversion 
is necessary and customer can save the extra CPU used for texture creation.

Building instructions:

This application requires static libraries and header files from v4l-utils package, 
where this package is under GNU LGPL2.1 licensing. The files are located in 
<path/to/dma-test> folder.    

Current v4l-utils package version is 1.16.6.
This application is tested on Clear Linux, it might not work on other Linux distribution. 
This application is tested with GCC version: 8.3.1, it might not be able to compile with a 
lower version.

Steps to update/acquire newer version of static libraries and header files:
  
1. Download v4l-utils package from https://www.linuxtv.org/downloads/v4l-utils/
2. Extract the downloaded package, eg. $tar -xvf v4l-utils-1.16.6.tar.bz2
3. $cd v4l-utils-<version> 
4. Make the package by using command:
   $./bootstrap.sh
   $./configure
   $CFLAGS="-fPIC" make
   Please refer to the README file for more details. 
5. Find the 2 header files below in ../utils/media-ctl
	a. mediactl.h
	b. v4l2subdev.h  
6. Find the 2 static library below in ../utils/media-ctl/.libs
        a. libmediactl.a
        b. libv4l2subdev.a
7. Replace the files in <path/to/dma-test>/
	$cp mediactl.h \
		v4l2subdev.h \
		.libs/libmediactl.a \
		.libs/libv4l2subdev.a \
		<path/to/dma-test>

Running Instructions:

    dma_test <options>

	Here is description of parameters;
        -d <path to device> video device to be used
        -I <W,H> input stream resolution
        -O <W,H> display steam resolution
        -i input stream is interlaced
        -n <port> port number (this is MRB specific port 0 is HDMI, port 4 is camera)
        -b <num> number of buffers to be used during streaming
        -f <format> input stream color format (for camera only UYVY is supported, for HDMI - UYVY RGB3, RGBP)
        -r <method> rendering method 
                           (GL - uses OpenGL, data from IPU is copied to userspace and then to GPU, 
                            GL_DMA - uses OpenGL - data from IPU is available to GPU without any copy needed, 
                            WL - uses wayland surface directly)
        -N <frames> number of frames to be streamed, after that application closes
        -E use DMA buffers

Examples:

To run HDMI:
dma_test -d /dev/video16 -n 0 -I 1920,1080 -O 1920,1080 -b 3 -f RGB3 -r GL_DMA 

To run camera:
dma_test -d /dev/video16 -i -n 4 -I 720,480 -O 1920,1080 -b 3 -f UYVY -r GL_DMA 


Example of output:

dma_test -d /dev/video16 -n 0 -I 1920,1080 -O 1920,1080 -b 3 -f RGB3 -r GL_DMA 
G_FMT(start): width = 1920, height = 1080, 4cc = XR24
G_FMT(final): width = 1920, height = 1080, 4cc = XR24
DMA TEST TIME STATS
Tracepoint                | System ts  | Time since app start
App start                 | 218.605 s |   0.00 ms
Media ctl setup           | 218.606 s |   0.99 ms
V4L2 setup                | 218.678 s |  73.52 ms
IPU streamon              | 218.687 s |  82.21 ms
Weston ready              | 218.687 s |  82.33 ms
EGL/GL setup              | 218.872 s | 267.13 ms
First frame received      | 218.715 s | 110.09 ms
First frame displayed     | 218.899 s | 293.80 ms
Received 300 frames from IPU in  5.000 seconds = 60.000 FPS
Rendered 298 frames in  5.000 seconds = 59.600 FPS
