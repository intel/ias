# Analysis

## Debugging Techniques

To get weston to generate logs, use the following commands:

    # killall weston
    # su -s /bin/sh -c 'XDG_RUNTIME_DIR=/run HOME=/home/root /usr/bin/weston >/tmp/weston.log 2>&1 &' root


To get weston to generate logs with more debug information, use the following commands:

    # killall weston
    # su -s /bin/sh -c 'XDG_RUNTIME_DIR=/run HOME=/home/root /usr/bin/weston --debug >/tmp/weston.log 2>&1 &' root

In both cases please ensure that the /tmp folder is writable.

To get the debug messages using the service, add following line to service file

	# Environment=WAYLAND_DEBUG=1

and use this command to get the messages.

	# journalctl -u weston.service

### Surfctrl Utility

The surfctrl client application has a --watch parameter. This option causes the 
program to monitor events and print them as they are received instead of exiting after 
displaying the surface information. This feature can be a helpful tool while debugging.

The utility surfctrl can also move around a surface and give you information on the 
surface. The syntax is as follows

    surfctrl --help 
    Usage: 
    surfctrl --surfname=<name of the surface> | --surfid=<id of the surface>
         [--pos=XxY]
         [--size=XxY]
         [--zorder=<0 based number> ]
         [--alpha=<0 based number>]
         [--watch=1]
    root@localhost:/usr/bin> 

### Layoutctrl Utility

    layoutctrl --help
    Usage:
    layoutctrl [--set-layout=X (Use 0 to deactivate plugin)
               [--output=Y] ]  (Use 0 for all outputs)

    layoutctrl
    Layout list

    ID: NAME
    -------------------------
    1: grid
    Output list
    ID: width x height (x,y) make model
    -------------------------
    11: 800x600 (0,0) unknown unknown

The layoutctrl client application allows users to see what outputs are defined by 
ias.conf and shows the layout plugins defined. This also allows deactivation and 
activation of any layout plugin and assigning it to any output.

For example, if a layout plugin has been deferred, it can be activated subsequently by 
using the following line.

    layoutctrl --set-layout=1

### Input Injector Utility

#### User Guide 

This input test tool has been designed to target hardware with \IASPrgName. It aims to help developers/users 
send inputs, ie keyboard, touch, mouse, from a host machine to the evdev interface of the target. The inputs 
then will be read by Wayland from the relevant device node and executed on the screen on the appropriate 
client application.
<P>

The input_injector rpm installs files in /opt/graphics/bin directory, it contains:
<P>

<ol>
<li>input_injector: This daemon is run on the target and collects the inputs from the host machine.</li>
<li>in.txt: A sample inputs file with keyboard strokes listed.  The ¡®connect¡¯ command at the top initiates the connection between target and host while specifying the height and width of the target display.</li>
<li>input_injector_test: Application to be run on the host.</li>
<li>and .so and .h files: Files needed to support the application</li>
</ol>

<P>
It is possible to specify the device node to where you want the input sent from the test application.  
Previously all events went to the default device.  Since the change, the test application can specify 
which device node it wants to inject an event to as the last parameter of any function This is an 
optional parameter and can be skipped if not necessary. 
If it is not specified the first keyboard, mouse, or device touch will be sent the input.
<P>

<b>Type:</b>


     ls /dev/input

<P>

This will list out the available device nodes.  Ie, event0, event1 etc.  Each time a new device is connected 
to the target a new device will be listed in the inputs.  Once the desired event node has been selected, 
send input to that node as follows:
<P>

<ul>
<li><input_type> <input_value> <event node></li>
<li>For example: key_down 31 1 </li>
</ul>
<P>

There is also a RAW/PIXEL mode for touch input. In RAW mode, no translation of co-ordinates is done and the 
co-ordinates are injected as is. In PIXEL mode, screen co-ordinates of where the touch occurred are first 
translated into raw co-ordinates and then injected.
<P>

This is accomplished by the following command:
<P>

<ul>
<li>touch_down X Y <0 (for pixel mode)  / 1 (for RAW mode)> <dev_node> </li>
<li>For example: touch_down 200 300 0 1 Simulates a touch down at 200 x 300 in pixel mode through device node 1. </li> 
</ul>
<P>


The user can specify commands on the fly or send a string of inputs via the in.txt file(this could be helpful for 
scripting).
<P>


Things to note before running the input_injector_test application:
<P>

<ul>
<li>It is essential to run this on a network that does not have a proxy.</li> 
<li>All input devices need to be connected before input injector daemon is run.</li>
<li>All input devices need to be connected before the application that needs them is launched.</li>
<li>In in.txt file, the user must specify the height and width of the display as a parameter before 
launching the application.</li>
</ul>


#### Installation Guide 

On the target:

<ol>
<li>On target run:
    
    
    input_injector  &  (NOTE:  All necessary input devices need to be 
                        connected before running daemon and launching the apps that need them.)
    
    
</li>
<li>Launch two Weston clients for keyboard input and mouse:
    
    
    weston-terminal &
    weston-dnd &
    

</li>
<li> On the host machine:


    input_injector_test <Target's IP address OR name>  < in.txt


This text file is filled with a key_down/key_up sequence.  Running this file as above, sends these keys 
to the target.  This text file can then be edited afterwards for further varied testing to encompass all 
types of input as defined in table.</li>
</ol>
<P>

#### Function Key Table

<table>
<tr>
<th>Function</th>
<th>Parm</th>
<th>Description</th>
<th>Info</th>
<th>Result</th>
<th>Call</th>
</tr>

<tr>
<td>key_down</td>
<td>int key</td>
<td>Simulates a key press down</td>
<td>List of keys in linux/input.h</td>
<td>0=success 1=failure</td>
<td>int key_down(int key);</td>
</tr>

<tr>
<td>key_up</td>
<td>int key</td>
<td>Simulates a keyboard key release</td>
<td>Present in linux/input.h</td>
<td>0=success 1=failure</td>
<td>int key_up(int key);</td>
</tr>

<tr>
<td>mouse_move</td>
<td>int x int y</td>
<td>Simulates the mouse move by a user provided number of pixels X,Y (Can be positive or negative)</td>
<td></td>
<td>0=success 1=failure</td>
<td>int mouse_move(int x, int y);</td>
</tr>

<tr>
<td>left_mouse_button_down</td>
<td>none</td>
<td>Simulates the left mouse button press</td>
<td></td>
<td>0=success 1=failure</td>
<td>int left_mouse_button_down();</td>
</tr>

<tr>
<td>left_mouse_button_up</td>
<td>none</td>
<td>Simulates the left mouse button release</td>
<td></td>
<td>0=success 1=failure</td>
<td>int left_mouse_button_up();</td>
</tr>

<tr>
<td>mid_mouse_button_down</td>
<td>none</td>
<td>Simulates the mid mouse button press</td>
<td></td>
<td>0=success 1=failure</td>
<td>int mid_mouse_button_down();</td>
</tr>

<tr>
<td>mid_mouse_button_up</td>
<td>none</td>
<td>Simulates the mid mouse button release</td>
<td></td>
<td>0=success 1=failure</td>
<td>int mid_mouse_button_up();</td>
</tr>

<tr>
<td>right_mouse_button_down</td>
<td>none</td>
<td>Simulates the right mouse button press</td>
<td></td>
<td>0=success 1=failure</td>
<td>int right_mouse_button_down();</td>
</tr>

<tr>
<td>right_mouse_button_up</td>
<td>none</td>
<td>Simulates the right mouse button release</td>
<td></td>
<td>0=success 1=failure</td>
<td>int right_mouse_button_up();</td>
</tr>

<tr>
<td>touch_down</td>
<td>int x int y</td>
<td>The absolute pixel position in X, Y where the user wants to touch the screen</td>
<td></td>
<td>0=success 1=failure</td>
<td>int touch_down(int x, int y);</td>
</tr>

<tr>
<td>touch_up</td>
<td>none</td>
<td>Simulates the touch release</td>
<td></td>
<td>0=success 1=failure</td>
<td>int touch_up();</td>
</tr>

<tr>
<td>touch_move</td>
<td>int x int y</td>
<td>Simulates the touch move from one set of coordinates to the absolute pixel position in X, Y direction.</td>
<td></td>
<td>0=success 1=failure</td>
<td>int touch_move(int x, int y);</td>
</tr>

</table>


#### Keyboard Keys


* Event Types

1.	Event type 0 (EV_SYN)
2.	Event type 1 (EV_KEY)
3.	Event code 1 (KEY_ESC)
4.	Event code 2 (KEY_1)
5.	Event code 3 (KEY_2)
6.	Event code 4 (KEY_3)
7.	Event code 5 (KEY_4)
8.	Event code 6 (KEY_5)
9.	Event code 7 (KEY_6)
10.	Event code 8 (KEY_7)
11.	Event code 9 (KEY_8)
12.	Event code 10 (KEY_9)
13.	Event code 11 (KEY_0)
14.	Event code 12 (KEY_MINUS)
15.	Event code 13 (KEY_EQUAL)
16.	Event code 14 (KEY_BACKSPACE)
17.	Event code 15 (KEY_TAB)
18.	Event code 16 (KEY_Q)
19.	Event code 17 (KEY_W)
20.	Event code 18 (KEY_E)
21.	Event code 19 (KEY_R)
22.	Event code 20 (KEY_T)
23.	Event code 21 (KEY_Y)
24.	Event code 22 (KEY_U)
25.	Event code 23 (KEY_I)
26.	Event code 24 (KEY_O)
27.	Event code 25 (KEY_P)
28.	Event code 26 (KEY_LEFTBRACE)
29.	Event code 27 (KEY_RIGHTBRACE)
30.	Event code 28 (KEY_ENTER)
31.	Event code 29 (KEY_LEFTCTRL)
32.	Event code 30 (KEY_A)
33.	Event code 31 (KEY_S)
34.	Event code 32 (KEY_D)
35.	Event code 33 (KEY_F)
36.	Event code 34 (KEY_G)
37.	Event code 35 (KEY_H)
38.	Event code 36 (KEY_J)
39.	Event code 37 (KEY_K)
40.	Event code 38 (KEY_L)
41.	Event code 39 (KEY_SEMICOLON)
42.	Event code 40 (KEY_APOSTROPHE)
43.	Event code 41 (KEY_GRAVE)
44.	Event code 42 (KEY_LEFTSHIFT)
45.	Event code 43 (KEY_BACKSLASH)
46.	Event code 44 (KEY_Z)
47.	Event code 45 (KEY_X)
48.	Event code 46 (KEY_C)
49.	Event code 47 (KEY_V)
50.	Event code 48 (KEY_B)
51.	Event code 49 (KEY_N)
52.	Event code 50 (KEY_M)
53.	Event code 51 (KEY_COMMA)
54.	Event code 52 (KEY_DOT)
55.	Event code 53 (KEY_SLASH)
56.	Event code 54 (KEY_RIGHTSHIFT)
57.	Event code 55 (KEY_KPASTERISK)
58.	Event code 56 (KEY_LEFTALT)
59.	Event code 57 (KEY_SPACE)
60.	Event code 58 (KEY_CAPSLOCK)
61.	Event code 59 (KEY_F1)
62.	Event code 60 (KEY_F2)
63.	Event code 61 (KEY_F3)
64.	Event code 62 (KEY_F4)
65.	Event code 63 (KEY_F5)
66.	Event code 64 (KEY_F6)
67.	Event code 65 (KEY_F7)
68.	Event code 66 (KEY_F8)
69.	Event code 67 (KEY_F9)
70.	Event code 68 (KEY_F10)
71.	Event code 69 (KEY_NUMLOCK)
72.	Event code 70 (KEY_SCROLLLOCK)
73.	Event code 71 (KEY_KP7)
74.	Event code 72 (KEY_KP8)
75.	Event code 73 (KEY_KP9)
76.	Event code 74 (KEY_KPMINUS)
77.	Event code 75 (KEY_KP4)
78.	Event code 76 (KEY_KP5)
79.	Event code 77 (KEY_KP6)
80.	Event code 78 (KEY_KPPLUS)
81.	Event code 79 (KEY_KP1)
82.	Event code 80 (KEY_KP2)
83.	Event code 81 (KEY_KP3)
84.	Event code 82 (KEY_KP0)
85.	Event code 83 (KEY_KPDOT)
86.	Event code 85 (KEY_ZENKAKUHANKAKU)
87.	Event code 86 (KEY_102ND)
88.	Event code 87 (KEY_F11)
89.	Event code 88 (KEY_F12)
90.	Event code 89 (KEY_RO)
91.	Event code 90 (KEY_KATAKANA)
92.	Event code 91 (KEY_HIRAGANA)
93.	Event code 92 (KEY_HENKAN)
94.	Event code 93 (KEY_KATAKANAHIRAGANA)
95.	Event code 94 (KEY_MUHENKAN)
96.	Event code 95 (KEY_KPJPCOMMA)
97.	Event code 96 (KEY_KPENTER)
98.	Event code 97 (KEY_RIGHTCTRL)
99.	Event code 98 (KEY_KPSLASH)
100.	Event code 99 (KEY_SYSRQ)
101.	Event code 100 (KEY_RIGHTALT)
102.	Event code 102 (KEY_HOME)
103.	Event code 103 (KEY_UP)
104.	Event code 104 (KEY_PAGEUP)
105.	Event code 105 (KEY_LEFT)
106.	Event code 106 (KEY_RIGHT)
107.	Event code 107 (KEY_END)
108.	Event code 108 (KEY_DOWN)
109.	Event code 109 (KEY_PAGEDOWN)
110.	Event code 110 (KEY_INSERT)
111.	Event code 111 (KEY_DELETE)
112.	Event code 112 (KEY_MACRO)
113.	Event code 113 (KEY_MUTE)
114.	Event code 114 (KEY_VOLUMEDOWN)
115.	Event code 115 (KEY_VOLUMEUP)
116.	Event code 116 (KEY_POWER)
117.	Event code 117 (KEY_KPEQUAL)
118.	Event code 118 (KEY_KPPLUSMINUS)
119.	Event code 119 (KEY_PAUSE)
120.	Event code 121 (KEY_KPCOMMA)
121.	Event code 122 (KEY_HANGUEL)
122.	Event code 123 (KEY_HANJA)
123.	Event code 124 (KEY_YEN)
124.	Event code 125 (KEY_LEFTMETA)
125.	Event code 126 (KEY_RIGHTMETA)
126.	Event code 127 (KEY_COMPOSE)
127.	Event code 128 (KEY_STOP)
128.	Event code 140 (KEY_CALC)
129.	Event code 142 (KEY_SLEEP)
130.	Event code 143 (KEY_WAKEUP)
131.	Event code 155 (KEY_MAIL)
132.	Event code 156 (KEY_BOOKMARKS)
133.	Event code 157 (KEY_COMPUTER)
134.	Event code 158 (KEY_BACK)
135.	Event code 159 (KEY_FORWARD)
136.	Event code 163 (KEY_NEXTSONG)
137.	Event code 164 (KEY_PLAYPAUSE)
138.	Event code 165 (KEY_PREVIOUSSONG)
139.	Event code 166 (KEY_STOPCD)
140.	Event code 172 (KEY_HOMEPAGE)
141.	Event code 173 (KEY_REFRESH)
142.	Event code 183 (KEY_F13)
143.	Event code 184 (KEY_F14)
144.	Event code 185 (KEY_F15)
145.	Event code 217 (KEY_SEARCH)
146.	Event code 226 (KEY_MEDIA)


* Event type 4 (EV_MSC)

1.	Event code 4 (MSC_SCAN)

* Event type 17 (EV_LED)

1.	Event code 0 (LED_NUML)
2.	Event code 1 (LED_CAPSL)
3.	Event code 2 (LED_SCROLLL)

* Key Repeat Handling 

** Repeat type 20 (EV_REP)

1.	Repeat code 0 (REP_DELAY)
		Value	250

2.	Repeat code 1 (REP_PERIOD)
		Value	33

## Sample Programs

A number of sample programs exist. These sample programs can be used to verify the functionality of Weston.

To execute the sample programs :-

    export XDG_RUNTIME_DIR=/run/wayland
    weston-simple-egl &

The spinning triangle will show up. 

    surfctrl --surfname="weston-simple-egl" --pos=1960,0


Then

    surfctrl 

Client surface, "weston-simple-egl", was created id = 167987280 250 x 250 @ 1960, 0 zorder 
= 0x0, alpha = 255, pid = 2710, pname = weston-simple-egl

Client surface, "info", was created id = 169941504 0 x 0 @ 0, 0 zorder = 0x0, alpha = 
255, pid = 1587, pname = weston

Client surface, "desktop", was created id = 169940568 1920 x 1080 @ 2860, 0 zorder 
= 0x1000000, alpha = 255, pid = 1587, pname = weston

Client surface, "desktop", was created id = 170093776 940 x 1072 @ 0, 0 zorder = 
0x1000000, alpha = 255, pid = 1587, pname = weston

Client surface, "desktop", was created id = 170093464 940 x 1072 @ 1920, 0 zorder = 
0x1000000, alpha = 255, pid = 1587, pname = weston


## FAQ - Frequently Asked Questions

### What is the number of worker threads?

No limit set by drivers

### What is the size of the vbo buffers?

No limit set by drivers. Up to applications

### What is the number of rendering buffers?

Applications can have as many rendering buffers as they want. Mesa limits the rendering buffers for Wayland compositor to 3.

### Do we support swap interval support?

We support both eglSwapInterval=0 or 1.

### What EGL extensions do we support?

We support the following EGL extensions:

1.     EGL_EXT_buffer_age
2.     EGL_EXT_image_dma_buf_import
3.     EGL_KHR_create_context
4.     EGL_KHR_gl_renderbuffer_image
5.     EGL_KHR_gl_texture_2D_image
6.     EGL_KHR_gl_texture_cubemap_image
7.     EGL_KHR_image_base
8.     EGL_KHR_surfaceless_context
9.     EGL_MESA_configless_context
10.     EGL_MESA_drm_image
11.     EGL_WL_bind_wayland_display

### What GLES extensions do we support?

1.     GL_ANGLE_texture_compression_dxt1
2.     GL_ANGLE_texture_compression_dxt3
3.     GL_ANGLE_texture_compression_dxt5
4.     GL_APPLE_texture_max_level
5.     GL_ARB_gpu_shader5
6.     GL_ARB_shading_language_packing
7.     GL_ARM_rgba8
8.     GL_EXT_blend_minmax
9.     GL_EXT_color_buffer_float
10.  GL_EXT_color_buffer_half_float
11.  GL_EXT_debug_marker
12.  GL_EXT_discard_framebuffer
13.  GL_EXT_frag_depth
14.  GL_EXT_multi_draw_arrays
15.  GL_EXT_occlusion_query_boolean
16.  GL_EXT_packed_float
17.  GL_EXT_read_format_bgra
18.  GL_EXT_robustness
19.  GL_EXT_separate_shader_objects
20.  GL_EXT_separate_specular_color
21.  GL_EXT_shader_texture_lod
22.  GL_EXT_shadow_samplers
23.  GL_EXT_sRGB
24.  GL_EXT_texture_compression_dxt1
25.  GL_EXT_texture_compression_s3tc
26.  GL_EXT_texture_filter_anisotropic
27.  GL_EXT_texture_format_BGRA8888
28.  GL_EXT_texture_lod_bias
29.  GL_EXT_texture_rg
30.  GL_EXT_texture_storage
31.  GL_EXT_unpack_subimage
32.  GL_INTEL_performance_queries
33.  GL_INTEL_timer_query
34.  GL_OES_blend_equation_separate
35.  GL_OES_blend_func_separate
36.  GL_OES_blend_subtract
37.  GL_OES_compressed_ETC1_RGB8_texture
38.  GL_OES_compressed_paletted_texture
39.  GL_OES_depth24
40.  GL_OES_depth_texture
41.  GL_OES_EGL_image
42.  GL_OES_EGL_image_external
43.  GL_OES_element_index_uint
44.  GL_OES_fbo_render_mipmap
45.  GL_OES_fixed_point
46.  GL_OES_framebuffer_object
47.  GL_OES_get_program_binary
48.  GL_OES_mapbuffer
49.  GL_OES_packed_depth_stencil
50.  GL_OES_point_sprite
51.  GL_OES_read_format
52.  GL_OES_required_internalformat
53.  GL_OES_rgb8_rgba8
54.  GL_OES_standard_derivatives
55.  GL_OES_stencil8
56.  GL_OES_stencil_wrap
57.  GL_OES_surfaceless_context
58.  GL_OES_texture_3D
59.  GL_OES_texture_cube_map
60.  GL_OES_texture_float
61.  GL_OES_texture_float_linear
62.  GL_OES_texture_half_float
63.  GL_OES_texture_half_float_linear
64.  GL_OES_texture_mirrored_repeat
65.  GL_OES_texture_npot
66.  GL_OES_vertex_array_object
67.  GL_OES_vertex_half_float
68.  GL_SUN_multi_draw_arrays

### What are the supported buffer formats e.g. pbuffer, pixmap?

Wayland does not support pbuffer or pixmap. It only has supports EGL Windows and it also supports FBOs.

### What pixel formats do we support? 

In Wayland environment and on Baytrail SoC, the following pixel formats are supported:

1.     RGB 565
2.     XRGB 8888
3.     ARGB 8888
4.     YUV 420
5.     YUV 422
6.     YUV 444
7.     NV 12

### Is eglContext sharing permitted?

Yes, egl context sharing is permitted in the supported Mesa version for Baytrail SoC in Waland environment


### What anti-aliasing filter are supportered?

On Baytrail SoC, our supported Mesa version has 4x MSAA support in it. Applications can develop shader based algorithms like CMAA and FXAA to do anti-aliasing.

### What Kernal Module options are supported?

We support i915 DRM. All of the kernel module options are open source. Here's the list of options that can be found in the latest i915:

1. modeset
2. panel_ignore_lid
3. semaphores
4. enable_rc6
5. enable_fbc
6. lvds_channel_mode
7. lvds_use_ssc
8. vbt_sdvo_panel_type
9. reset
10. enable_hangcheck
11. enable_ppgtt
12. enable_execlists
13. enable_psr
14. preliminary_hw_support
15. disable_power_well
16. enable_ips
17. prefault_disable
18. load_detect_test
19. invert_brightness
20. disable_display
21. disable_vtd_wa
22. enable_cmd_parser
23. use_mmio_flip
24. mmio_debug
25. verbose_state_checks
26. nuclear_pageflip
27. edp_vswing
28. enable_guc_submission
29. guc_log_level

### What UFO Extensions for EGL are supported?

We support the following :-

1. EGL_EXT_buffer_age
2. EGL_EXT_create_context_robustness
3. EGL_EXT_image_dma_buf_import
4. EGL_EXT_swap_buffers_with_damage
5. EGL_KHR_create_context
6. EGL_KHR_get_all_proc_addresses
7. EGL_KHR_gl_renderbuffer_image
8. EGL_KHR_gl_texture_2D_image
9. EGL_KHR_gl_texture_cubemap_image
10. EGL_KHR_image_base
11. EGL_KHR_surfaceless_context
12. EGL_MESA_configless_context
13. EGL_MESA_drm_image
14. EGL_WL_bind_wayland_display
15 EGL_WL_create_wayland_buffer_from_image

### What UFO Extensions for OGLES 1.0 are supported?

We support the following :-

1. GL_EXT_blend_minmax
2. GL_EXT_multi_draw_arrays
3. GL_EXT_texture_filter_anisotropic
4. GL_EXT_texture_compression_s3tc
5. GL_EXT_texture_lod_bias
6. GL_EXT_map_buffer_range
7. GL_INTEL_performance_query
8. GL_EXT_texture_storage
9. GL_KHR_debug
10. GL_OES_EGL_image
11. GL_OES_framebuffer_object
12. GL_OES_depth24
13. GL_OES_stencil8
14. GL_OES_packed_depth_stencil
15. GL_OES_rgb8_rgba8
16. GL_OES_byte_coordinates
17. GL_OES_point_sprite
18. GL_OES_point_size_array
19. GL_OES_blend_subtract
20. GL_OES_blend_func_separate
21. GL_OES_blend_equation_separate
22. GL_OES_single_precision
23. GL_OES_matrix_get
24. GL_OES_query_matrix
25. GL_OES_read_format
26. GL_OES_mapbuffer
27. GL_EXT_discard_framebuffer
28. GL_EXT_texture_format_BGRA8888
29. GL_OES_compressed_paletted_texture
30. GL_OES_EGL_image_external
31. GL_OES_compressed_ETC1_RGB8_texture
32. GL_OES_fixed_point
33. GL_OES_draw_texture
34. GL_OES_vertex_array_object
35. GL_OES_texture_cube_map
36. GL_OES_fbo_render_mipmap
37. GL_OES_stencil_wrap
38. GL_OES_element_index_uint
39. GL_OES_texture_npot
40. GL_OES_texture_mirrored_repeat
41. GL_OES_texture_env_crossbar
42. GL_EXT_sRGB
43. GL_APPLE_texture_max_level
44. GL_EXT_texture_compression_dxt1
45. GL_OES_required_internalformat
46. GL_OES_surfaceless_context
47. GL_EXT_robustness
48. GL_EXT_texture_sRGB_decode
49. GL_EXT_read_format_bgra
50. GL_EXT_debug_marker

### What UFO Extensions for OGLES 2.0 are supported?

We support the following :-

1. GL_APPLE_texture_max_level
2. GL_EXT_blend_minmax
3. GL_EXT_buffer_storage
4. GL_EXT_color_buffer_float
5. GL_EXT_color_buffer_half_float
6. GL_EXT_copy_image
7. GL_EXT_debug_marker
8. GL_EXT_discard_framebuffer
9. GL_EXT_disjoint_timer_query
10. GL_EXT_draw_buffers
11. GL_EXT_draw_buffers_indexed
12. GL_EXT_draw_elements_base_vertex
13. GL_EXT_draw_instanced
14. GL_EXT_frag_depth
15. GL_EXT_geometry_point_size
16. GL_EXT_geometry_shader
17. GL_EXT_instanced_arrays
18. GL_EXT_map_buffer_range
19. GL_EXT_multi_draw_arrays
20. GL_EXT_occlusion_query_boolean
21. GL_EXT_read_format_bgra
22. GL_EXT_robustness
23. GL_EXT_separate_shader_objects
24. GL_EXT_shader_framebuffer_fetch
25. GL_EXT_shader_integer_mix
26. GL_EXT_shader_texture_lod
27. GL_EXT_shadow_samplers
28. GL_EXT_sRGB
29. GL_EXT_sRGB_write_control
30. GL_EXT_texture_border_clamp
31. GL_EXT_texture_compression_dxt1
32. GL_EXT_texture_compression_s3tc
33. GL_EXT_texture_filter_anisotropic
34. GL_EXT_texture_format_BGRA8888
35. GL_EXT_texture_rg
36. GL_EXT_texture_sRGB_decode
37. GL_EXT_texture_storage
38. GL_EXT_unpack_subimage
39. GL_EXT_YUV_target
40. GL_INTEL_framebuffer_CMAA
41. GL_INTEL_geometry_shader
42. GL_INTEL_multi_rate_fragment_shader
43. GL_INTEL_performance_query
44. GL_KHR_blend_equation_advanced
45. GL_KHR_blend_equation_advanced_coherent
46. GL_KHR_debug
47. GL_KHR_robustness
48. GL_KHR_texture_compression_astc_hdr
49. GL_KHR_texture_compression_astc_ldr
50. GL_NV_polygon_mode
51. GL_OES_compressed_ETC1_RGB8_texture
52. GL_OES_compressed_paletted_texture
53. GL_OES_copy_image
54. GL_OES_depth24
55. GL_OES_depth_texture
56. GL_OES_depth_texture_cube_map
57. GL_OES_draw_buffers_indexed
58. GL_OES_draw_elements_base_vertex
59. GL_OES_EGL_image
60. GL_OES_EGL_image_external
61. GL_OES_element_index_uint
62. GL_OES_fbo_render_mipmap
63. GL_OES_geometry_point_size
64. GL_OES_geometry_shader
65. GL_OES_get_program_binary
66. GL_OES_mapbuffer
67. GL_OES_packed_depth_stencil
68. GL_OES_required_internalformat
69. GL_OES_rgb8_rgba8
70. GL_OES_sample_shading
71. GL_OES_sample_variables
72. GL_OES_shader_image_atomic
73. GL_OES_shader_multisample_interpolation
74. GL_OES_standard_derivatives
75. GL_OES_surfaceless_context
76. GL_OES_texture_3D
77. GL_OES_texture_border_clamp
78. GL_OES_texture_float
79. GL_OES_texture_float_linear
80. GL_OES_texture_half_float
81. GL_OES_texture_half_float_linear
82. GL_OES_texture_npot
83. GL_OES_texture_stencil8
84. GL_OES_vertex_array_object
85. GL_OES_vertex_half_float

### What UFO Extensions for OGLES 3.0 are supported?

We support the following :-

1. GL_APPLE_texture_max_level
2. GL_EXT_blend_minmax
3. GL_EXT_buffer_storage
4. GL_EXT_color_buffer_float
5. GL_EXT_color_buffer_half_float
6. GL_EXT_copy_image
7. GL_EXT_debug_marker
8. GL_EXT_discard_framebuffer
9. GL_EXT_disjoint_timer_query
10. GL_EXT_draw_buffers
11. GL_EXT_draw_buffers_indexed
12. GL_EXT_draw_elements_base_vertex
13. GL_EXT_draw_instanced
14. GL_EXT_frag_depth
15. GL_EXT_geometry_point_size
16. GL_EXT_geometry_shader
17. GL_EXT_instanced_arrays
18. GL_EXT_map_buffer_range
19. GL_EXT_multi_draw_arrays
20. GL_EXT_occlusion_query_boolean
21. GL_EXT_read_format_bgra
22. GL_EXT_robustness
23. GL_EXT_separate_shader_objects
24. GL_EXT_shader_framebuffer_fetch
25. GL_EXT_shader_integer_mix
26. GL_EXT_shader_texture_lod
27. GL_EXT_shadow_samplers
28. GL_EXT_sRGB
29. GL_EXT_sRGB_write_control
30. GL_EXT_texture_border_clamp
31. GL_EXT_texture_compression_dxt1
32. GL_EXT_texture_compression_s3tc
33. GL_EXT_texture_filter_anisotropic
34. GL_EXT_texture_format_BGRA8888
35. GL_EXT_texture_rg
36. GL_EXT_texture_sRGB_decode
37. GL_EXT_texture_storage
38. GL_EXT_unpack_subimage
39. GL_EXT_YUV_target
40. GL_INTEL_framebuffer_CMAA
41. GL_INTEL_geometry_shader
42. GL_INTEL_multi_rate_fragment_shader
43. GL_INTEL_performance_query
44. GL_KHR_blend_equation_advanced
45. GL_KHR_blend_equation_advanced_coherent
46. GL_KHR_debug
47. GL_KHR_robustness
48. GL_KHR_texture_compression_astc_hdr
49. GL_KHR_texture_compression_astc_ldr
50. GL_NV_polygon_mode
51. GL_OES_compressed_ETC1_RGB8_texture
52. GL_OES_compressed_paletted_texture
53. GL_OES_copy_image
54. GL_OES_depth24
55. GL_OES_depth_texture
56. GL_OES_depth_texture_cube_map
57. GL_OES_draw_buffers_indexed
58. GL_OES_draw_elements_base_vertex
59. GL_OES_EGL_image
60. GL_OES_EGL_image_external
61. GL_OES_element_index_uint
62. GL_OES_fbo_render_mipmap
63. GL_OES_geometry_point_size
64. GL_OES_geometry_shader
65. GL_OES_get_program_binary
66. GL_OES_mapbuffer
67. GL_OES_packed_depth_stencil
68. GL_OES_required_internalformat
69. GL_OES_rgb8_rgba8
70. GL_OES_sample_shading
71. GL_OES_sample_variables
72. GL_OES_shader_image_atomic
73. GL_OES_shader_multisample_interpolation
74. GL_OES_standard_derivatives
75. GL_OES_surfaceless_context
76. GL_OES_texture_3D
77. GL_OES_texture_border_clamp
78. GL_OES_texture_float
79. GL_OES_texture_float_linear
80. GL_OES_texture_half_float
81. GL_OES_texture_half_float_linear
82. GL_OES_texture_npot
83. GL_OES_texture_stencil8
84. GL_OES_vertex_array_object
85. GL_OES_vertex_half_float

### What UFO Extensions for OGLES 3.1 are supported?

We support the following :-

1. GL_ANDROID_extension_pack_es31a
2. GL_APPLE_texture_max_level
3. GL_EXT_blend_minmax
4. GL_EXT_buffer_storage
5. GL_EXT_color_buffer_float
6. GL_EXT_color_buffer_half_float
7. GL_EXT_copy_image
8. GL_EXT_debug_marker
9. GL_EXT_discard_framebuffer
10. GL_EXT_disjoint_timer_query
11. GL_EXT_draw_buffers
12. GL_EXT_draw_buffers_indexed
13. GL_EXT_draw_elements_base_vertex
14. GL_EXT_draw_instanced
15. GL_EXT_frag_depth
16. GL_EXT_geometry_point_size
17. GL_EXT_geometry_shader
18. GL_EXT_gpu_shader5
19. GL_EXT_instanced_arrays
20. GL_EXT_map_buffer_range
21. GL_EXT_multi_draw_arrays
22. GL_EXT_occlusion_query_boolean
23. GL_EXT_primitive_bounding_box
24. GL_EXT_read_format_bgra
25. GL_EXT_robustness
26. GL_EXT_separate_shader_objects
27. GL_EXT_shader_framebuffer_fetch
28. GL_EXT_shader_integer_mix
29. GL_EXT_shader_io_blocks
30. GL_EXT_shader_texture_lod
31. GL_EXT_shadow_samplers
32. GL_EXT_sRGB
33. GL_EXT_sRGB_write_control
34. GL_EXT_tessellation_point_size
35. GL_EXT_tessellation_shader
36. GL_EXT_texture_border_clamp
37. GL_EXT_texture_buffer
38. GL_EXT_texture_compression_dxt1
39. GL_EXT_texture_compression_s3tc
40. GL_EXT_texture_cube_map_array
41. GL_EXT_texture_filter_anisotropic
42. GL_EXT_texture_format_BGRA8888
43. GL_EXT_texture_rg
44. GL_EXT_texture_sRGB_decode
45. GL_EXT_texture_storage
46. GL_EXT_unpack_subimage
47. GL_EXT_YUV_target
48. GL_INTEL_fragment_shader_ordering
49. GL_INTEL_framebuffer_CMAA
50. GL_INTEL_geometry_shader
51. GL_INTEL_multi_rate_fragment_shader
52. GL_INTEL_performance_query
53. GL_INTEL_tessellation
54. GL_KHR_blend_equation_advanced
55. GL_KHR_blend_equation_advanced_coherent
56. GL_KHR_debug
57. GL_KHR_robustness
58. GL_KHR_texture_compression_astc_hdr
59. GL_KHR_texture_compression_astc_ldr
60. GL_NV_polygon_mode
61. GL_OES_compressed_ETC1_RGB8_texture
62. GL_OES_compressed_paletted_texture
63. GL_OES_copy_image
64. GL_OES_depth24
65. GL_OES_depth_texture
66. GL_OES_depth_texture_cube_map
67. GL_OES_draw_buffers_indexed
68. GL_OES_draw_elements_base_vertex
69. GL_OES_EGL_image
70. GL_OES_EGL_image_external
71. GL_OES_element_index_uint
72. GL_OES_fbo_render_mipmap
73. GL_OES_geometry_point_size
74. GL_OES_geometry_shader
75. GL_OES_get_program_binary
76. GL_OES_gpu_shader5
77. GL_OES_mapbuffer
78. GL_OES_packed_depth_stencil
79. GL_OES_primitive_bounding_box
80. GL_OES_required_internalformat
81. GL_OES_rgb8_rgba8
82. GL_OES_sample_shading
83. GL_OES_sample_variables
84. GL_OES_shader_image_atomic
85. GL_OES_shader_io_blocks
86. GL_OES_shader_multisample_interpolation
87. GL_OES_standard_derivatives
88. GL_OES_surfaceless_context
89. GL_OES_tessellation_point_size
90. GL_OES_tessellation_shader
91. GL_OES_texture_3D
92. GL_OES_texture_border_clamp
93. GL_OES_texture_buffer
94. GL_OES_texture_cube_map_array
95. GL_OES_texture_float
96. GL_OES_texture_float_linear
97. GL_OES_texture_half_float
98. GL_OES_texture_half_float_linear
99. GL_OES_texture_npot
100. GL_OES_texture_stencil8
101. GL_OES_texture_storage_multisample_2d_array
102. GL_OES_vertex_array_object
103. GL_OES_vertex_half_float
