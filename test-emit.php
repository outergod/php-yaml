<?php

ini_set ('yaml.nomnom', 1);

$array1 = array ('foo' => 'bar',
                 array ('a', 'b'),
                 array ('c' => 'd'),
                 null,
                 2,
                 false,
                 true,
                 1.1);

$array2 = array ('a', 'b', 'c', 1, 2, 3);

$array3 = array ($array1, $array2);

$scalar1 = 'äöüß';

$scalar2 = <<<EOD
Lorem ipsum dolor sit amet, consectetuer adipiscing elit. Donec non turpis. Aliquam ultricies diam a felis. Nunc vel mauris. Phasellus at tortor. Phasellus in quam fermentum lectus semper posuere. Aliquam sodales urna nec lectus pretium sodales. Lorem ipsum dolor sit amet, consectetuer adipiscing elit. Duis velit velit, semper vitae, consectetuer nec, hendrerit ac, massa. Donec iaculis luctus nisl. Vestibulum diam. Nam ac massa vel neque accumsan consequat. Maecenas est. Donec feugiat nisi sit amet sapien interdum commodo. Curabitur sollicitudin tincidunt velit. Etiam aliquam.

Lorem ipsum dolor sit amet, consectetuer adipiscing elit. Donec non turpis. Aliquam ultricies diam a felis. Nunc vel mauris. Phasellus at tortor. Phasellus in quam fermentum lectus semper posuere. Aliquam sodales urna nec lectus pretium sodales. Lorem ipsum dolor sit amet, consectetuer adipiscing elit. Duis velit velit, semper vitae, consectetuer nec, hendrerit ac, massa. Donec iaculis luctus nisl. Vestibulum diam. Nam ac massa vel neque accumsan consequat. Maecenas est. Donec feugiat nisi sit amet sapien interdum commodo. Curabitur sollicitudin tincidunt velit. Etiam aliquam.
EOD;

$scalar2 = array ('a' => $scalar2);

$arrays = array ($array1, $array2, $array3, $scalar1, $scalar2);

foreach ($arrays as $n => $array)
  {
    echo "================================================================================\n";
    echo "$n:\n";
    echo "================================================================================\n";
    /* $output = yaml_emit_file ("file-$n.yaml", $array); */
    $output = yaml_emit ($array, YAML_UTF8_ENCODING);
    echo print_r ($output, true) ?: "ERROR";
    echo PHP_EOL;
  }
