# Overview 

Graphics Subsystem

The mission of the Graphics Subsystem is to provide high-quality visual feedback in a 
multi-display automotive environment.<br>

The Graphic Subsystem delivers the capabilities to meet the automotive requirements for 
visual feedback in the vehicle interior. This means providing in-time visualization to driver, 
passenger & rear seat entertainment from sources like internet, 3rd party applications & 
devices, navigation & DVD/Blue ray players where source and sink can be front units and 
rear seat units in any configuration.<br>

A challenge is providing a suitable data transfer rate if passengers use more than one data 
source.<br>

There are natural cross dependencies between the Graphic Subsystem, the system as a 
whole and provided hardware / drivers: Due to the fact, that some graphic calculations & 
driver calls are done on the CPU, the final graphic performance is both, limited by the 
chosen hardware & limited by current CPU and GPU load of the running system.
In general increasing screen sizes, dual view screens and extensions to head-up displays 
and in-cluster displays sharing GPU among several applications with different output to 
parts of displays (split screen) or multiple displays will increase requirements regarding 
computational power and possibility to share it.
<P>

<table>
<tr>
  <th>Component</th>
  <th>Version</th>
  <th>Upstream commit</th>
</tr>

<tr>
  <td>Weston</td>
  <td>1.10.0</td>
  <td>Repo: git://anongit.freedesktop.org/wayland/weston <br>
  Branch: 1.10 <br>
  Commit: d45de283ce1f9edafc6f33632fc917513c620912</td>
</tr>


<tr>
  <td>Layer Manager Controller</td>
  <td>1.3.92</td>
  <td> Repo: git://git.projects.genivi.org/wayland-ivi-extension.git <br>
  Tag: 1.3.92 </td>
</tr>

</table>
