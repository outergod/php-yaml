<?php
$dir = opendir ('examples');
while (false !== ($file = readdir ($dir)))
  {
    if (basename ($file, 'yaml') !== $file)
      $files[] = 'examples/'.$file;
  }
closedir ($dir);

natsort ($files);

foreach ($files as $file)
  {
    echo "================================================================================\n";
    echo "$file:\n";
    echo "================================================================================\n";
    $input = file_get_contents ($file);
    $output = yaml_parse ($input, -1, $ndocs);

    if ($output)
      print_r ($output);
    else
      echo "ERROR";
    echo PHP_EOL;
  }
