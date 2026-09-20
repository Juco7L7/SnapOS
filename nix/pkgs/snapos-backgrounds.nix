{ runCommand }:

runCommand "snapos-backgrounds" { } ''
  d=$out/share/backgrounds/snapos
  mkdir -p $d $out/share/gnome-background-properties
  cp ${../../branding/wallpapers/snapos-default.jpeg} $d/snapos-dark.jpeg
  cp ${../../branding/wallpapers/snapos-light.jpeg}   $d/snapos-light.jpeg

  cat > $out/share/gnome-background-properties/snapos.xml <<XML
  <?xml version="1.0"?>
  <!DOCTYPE wallpapers SYSTEM "gnome-wp-list.dtd">
  <wallpapers>
    <wallpaper deleted="false">
      <name>SnapOS Dark</name>
      <filename>$d/snapos-dark.jpeg</filename>
      <options>zoom</options>
      <shade_type>solid</shade_type>
      <pcolor>#141110</pcolor>
      <scolor>#000000</scolor>
    </wallpaper>
    <wallpaper deleted="false">
      <name>SnapOS Light</name>
      <filename>$d/snapos-light.jpeg</filename>
      <options>zoom</options>
      <shade_type>solid</shade_type>
      <pcolor>#ECE9E5</pcolor>
      <scolor>#FFFFFF</scolor>
    </wallpaper>
  </wallpapers>
  XML
''
