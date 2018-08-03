# Configuration {#configuration}

## Environment variables

The following environment variables are required

    # export HOME=
	# This export will point to where the current ias.conf is located
    # export XDG_RUNTIME_DIR=/run/wayland

Also the following are available 

1. LIBGL_DRIVERS_PATH selecting the directory for EGL to search for when loading a DRI driver for a specific EGL based process.
2. LIBVA_DRIVERS_PATH selecting the directory for libva to search for when loading a VAAPI driver for a specific video process.

## Search path

The following search path has to be set: (Normally set by default)

      LD_LIBRARY_PATH=/opt/ias/lib

## Configuration files

The ias-shell supports the loading of a custom client that is privileged and can 
control some aspects of client layout. This scenario is similar to the desktop-shell and 
desktop-shell client in Weston, where the desktop-shell client may present a global 
menu bar and desktop menus to launch other client applications. The ias-shell
client and layout plugins have some overlap in functionality. 
The ias-shell's configuration is controlled by the ias.conf XML file. The file 
ias.conf is located in the same directory as the weston.ini file (/home/root/.config). The ias.conf 
file contains a section, <shell>, that defines the controller 
For example:


    <shell>
        <hmi exec='path_to_hmi_application' />
    </shell>


### weston.ini

The weston.ini file is the initialization file for the compositor:

    ${HOME}/.config/weston.ini
For ivi shell hmi, use weston.ini.ivihmi as weston.ini

Its basic contents should include:

    [core]
    modules=ias-shell.so, ias-plugin-framework.so

This configuration file specifies that the IAS shell should be used and that the IAS 
plug-in framework should also be loaded


###ias.conf

The ias.conf file is found in the following path:

    ${HOME}/.config/ias.conf

The following is an example ias.conf file:

Two HDMI Screen Example :-

    <?xml version='1.0' encoding='UTF-8'?>
    <iasconfig>
          <shell>
                    <input_plugin name="TSD_Input" lib="/usr/lib/ias/input.so" /> 
                        <!--Input plugin for handling keys, pointers and touches --> 
                    <plugin name="TSD_Main" lib="/usr/lib/ias/grid_layout.so" defer="1"/>
                        <!-- layout plugin to handle services - defer means loaded after weston starts -->
          </shell>
          <backend raw_keyboards='1'> <!-- this loads smaller keyboard driver -->
                    <!-- Use Mesa instead of UFO DRI drivers for Weston -->
                    <!-- so weston will use weston to start up all other apps will use ufo -->
                    <env var="GBM_DRIVERS_PATH" val="/usr/lib/mesadri" />
                    <startup>
                            <!-- this is the first hdmi channel classic mode means single screen -->
                            <crtc name='HDMI1' model="classic" mode='preferred'>
                                    <output name='HDMI1-0' size='inherit' position='origin'>
                                    </output>
                            </crtc>
                            <!-- cluster output -->
                            <crtc name="HDMI2" model="classic" mode="1280x480">
                                    <output name="HDMI2-0" size="inherit" position="rightof" target="HDMI1-0">
                                    </output>
                            </crtc>
                    </startup>
          </backend>
    </iasconfig>

<P>
Flexible Model Example 

    <?xml version="1.0" encoding="UTF-8"?>
    <iasconfig>
        <shell>
        </shell>
        <backend raw_keyboards='1'>
            <!-- Use Mesa instead of UFO DRI drivers for Weston -->
        <env var="GBM_DRIVERS_PATH" val="/usr/lib/mesadri" />
            <startup>
                <crtc name="HDMI1" model="flexible" mode="preferred">
                    <output name="Main" position="origin" />
                        <!-- Plane for left view -->
                    <output name="Left" plane_size="480x800" position="rightof" target="Main" plane_position="0,0" rotation="270" />
                        <!-- Plane for right view -->
                    <output name="Right" plane_size="480x800" position="rightof" target="Left" plane_position="512,0" rotation="270" />
                </crtc>
                <crtc name="HDMI2" model="classic" mode="1280x480">
                    <output name="Cluster" size="inherit" position="rightof" target="Right">                        
                    </output>
                </crtc>
            </startup>
        </backend>
        <!-- Input Plugin -->
        <input_plugin name="TSD_Input" lib="/usr/lib/ias/input.so" />
            <!-- left hand layout plugin -->
        <plugin name="TSD_Left" lib="/usr/lib/ias/grid_layout.so" defer="1"/>
    </iasconfig>

<P>
Clone Mode Example 

To enable clone mode you need to set both HDMI ports (crtc) to have position=origin
This means that a client applications displays the surface at the 0,0 position of both screens


    <?xml version='1.0' encoding='UTF-8'?>
    <iasconfig>
        <backend>
            <startup>
                <crtc name='HDMI1' model='classic' mode='preferred'>
                    <output name='HDMI1-0' size='inherit' position='origin'>
                    </output>
                </crtc>
                <crtc name='HDMI2' model='classic' mode='preferred'>
                    <output name='HDMI2-0' size='inherit' position='origin'>
                    </output>
                </crtc>
            </startup>
        </backend>
    </iasconfig>



<P>

<ol>
<li>hmi exec is the path to the custom HMI application. For demo purposes, it points to Intel's basic sample HMI.</li>
<li>plugin name is the path to the custom layout plugin. For demo purposes, it points to Intel's basic sample layout plugin. This plugin can be loaded and activated , loaded or deferred<</li>
<li>Input_plugin name is the path to the custom layout plugin. For demo purposes it points to Intel's basic sample input plugin. This plugin is automatically loaded on weston startup.</li>
</ol>

<P>
Atomic Page Flip

To enable atomic/nuclear page flip, please ensure the linux kernel has the following option enabled.


    i915.nuclear_pageflip=1


In the ias.conf file ensure it has the foollowing settings



    <?xml version='1.0' encoding='UTF-8'?>
    <iasconfig>
        <backend print_fps='1' raw_keyboards='1'>
            :
            :
            :


<ol>
<li>print_fps prints frames per second for all surfaces</li>
<li>raw_keyboards loads only raw driver</li>
<li>use_nuclear_flip='0' to disable atomic page flip</li>
</ol>



<P>
###ias-backend

The Intel Automotive Solutions Compositor backend module is what enables the use of layout plugins and provides the interface i
to the Intel® Embedded Media and Graphics Driver (Intel® EMGD) kernel mode driver. It takes advantage of specific 
capabilities of the Intel® EMGD driver. 
<P>
The backend module will also work with the standard i965 kernel mode driver, but the following capabilities will not function:
<ol>
<li>frame buffer gamma correction</li>
<li>frame buffer contrast adjustment</li>
<li>frame buffer brightness adjustment</li>
</ol>
<P>
Backend option examples are found in the ias.conf file:
<P>

    <backend depth='1' stencil='1' >
        <startup>
            <crtc name='HDMI1' model='classic' mode='preferred'>
                <output name='HDMI1-0' size='inherit' position='origin' />
            </crtc>
        </startup>
    </backend>

<P>

Also there are options for making a flexible crtc model for HDMI2 and classic for HDMI1:

    <backend depth='1' stencil='1' raw_keyboards='1'>
        <startup>
            <crtc name='HDMI1' model='classic' mode='preferred'>
                <output name='HDMI1-0' size='inherit' position='origin' />
            </crtc>
            <crtc name='HDMI2' model='flexible' mode='preferred'>
                <output name='HDMI1-0' size='inherit' position='origin' />
                <output name='LEFT' size='scale:400x400' position='rightof' target='HDMI1-0' plane_position='10,10' plane_size='600x800' />
                <output name='RIGHT' size='inherit' position='rightof'target='LEFT' plane_position='620,10' plane_size='600x800' rotation='270' />
            </crtc>
        </startup>
    </backend>


<P>
EGL driver at context creation time. Examples of what the values can be are integers '0', '1', '8', '16' 
<P>
<ul>
<li>backend stencil - whatever value is passed here will be passed to the underlying EGL driver at context creation time. 
Examples of what the values can be are integers '0', '1', '8', '16' </li>
<li>backend normalized_rotation - Some clients do not pay attention to the transform field in the geometry event. Ideally, 
clients are expected to swap their widths and heights while submitting full-screen buffers if the rotation value 
is 90 or 270. If the clients do not do this, to avoid having the client code rewritten, the normalized_rotation 
flag can be turned on and this would enable the compositor to send the swapped widths and heights of outputs and a transform 
of 0. The normalized_rotation flag is turned off by default. With this variable a transform flag is set telling the client 
to create a transform for the screen if there needs to be one. For example, with normalize rotation a 500x400 image rotated 
because a 400x500 image. Without normalized rotation and a screen of the following occurs:
<P>
Rotating a surface with a screen 800x600 to 600x800
<P>
<ul>
<li>800/600 = 1.33</li>
<li>600/800 = 0.75</li>
</ul>
<P>
So a 500x400 surface becomes 665x300.<P>
</li>
<li>crtc name - CRTC names must match the "TYPE#," where "TYPE" is "HDMI," "VGA," and so on and "#" is the kernel numbering, ie how the kernel addresses the screens. Use the following steps:

    
    $find / -name card0
    /sys/devices/pci0000:00/0000:00:02.0/drm/card0
    /sys/devices/platform/byt_bin_ak4614/sound/card0
    /sys/class/drm/card0
    /sys/class/sound/card0
    /proc/asound/card0
    /dev/dri/card0
        
    ~$cd /sys/devices/pci0000:00/0000:00:02.0/drm/card0
    card0$ls
    card0-HDMI-A-1 dev device power subsystem uevent
    


<P>
See :- card0-HDMI-A-<b>1</b> dev device power subsystem event 

<P><b>Please note the bold number above refers to the kernel numbering</b><P></li>

<li>To add a second screen there are a number of steps. EMGD needs to have a two display configuration in the kernel. 
Then in ias.conf add a new crtc tag giving the name of the new port, namely, HDMI2, depending on the port naming. 
Also the steps above can be re-run to determine the name of the second screen. </li>
<li>crtc models - can be 'classic', 'disabled', 'dualview', or 'flexible'. 
<ul>
<li>Classic - standard single display plane </li>
<li>Disabled - </li>
<li>Dual view - </li>
<li>Flexible - using two sprite planes for output </li>
</ul></li>
<li>Preferred means to basically take the native mode specified by the EMGD graphics driver. 
You can also use "inherit" which will take the current mode used by the kernel driver or specify a widthxheight. </li>
<li>output name - can be anything. It is used to identify this output. </li>
<li>output size - is either inherit or scale:wxh. Inherit means to take the crtc display size (as specified by the crtc "mode"). 
Scale takes a width x height and will scale the output from the specified size to the crtc output size. 
Scaling would make sense only in very specific cases. Say you have content that comes natively in 
800x600 and you always want it scaled to the display size. </li>
<li>output position - is the location of this output in Wayland virtual space. Origin means position it at 0,0. Other 
options are "rightof" and "below," which position it relative to other outputs and absolute position. </li>
<li>output target - is an output name to position this output relative to. This is used in conjunction with output position right of or 
below .output rotation - Compositor outputs may be rotated 0, 90, 180, or 270 degrees.</li>
</ul>
<P>
Now for setting up the sprite planes, the following line has additional meaning:
<P>

    <output name='LEFT' size='scale:400x400' position='rightof' target='HDMI1-0' plane_position='10,10' plane_size='600x800' />

<P>
<ul>
<li>output name - again can be anything that makes sense to identify the output. </li>
<li>Output size - as above, can be "inherit" or "scale". In most cases, you probably want inherit. </li>
<li>Output plane_size - is the actual dimensions of the sprite plane. In this case, we want a plane that is 600 wide by 800 tall. </li>
<li>Output plane_position - is where the sprite plane is located on the display output relative to the main display plane. The sprite plane needs to fit within the boundaries of the main display plane. </li>
<li>Output position - Position is the location of the output in Wayland virtual space. In this case, I'm putting it next to the main display plane, but it does not overlap. Overlapping outputs in this mode can cause some issues with frame events and is not recommended. "Position" and "plane_position" are two very different things.You don't want the outputs to overlay in virtual space, but you do want the planes to overlap on the physical display. </li>
<li>Output target - is used to position an output relative to another and specifies (by name) the output to position it relative to. </li>
<li>Output rotation - allows you to rotate the contents before it is displayed by 0, 90, 180, or 270 degrees </li>
</ul>

