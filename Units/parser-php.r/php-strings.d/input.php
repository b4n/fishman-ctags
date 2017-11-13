<?php

$a = "hello";

$b = 'hello';

$c = "hello \"buddy\"";

$d = 'hello \'buddy\'';

$e = <<<EOS
hello buddy
EOS;

$f = <<<'EOS'
hello buddy
EOS;

$g = <<<"EOS"
hello buddy
EOS;

# empty heredocs
$h = <<<EOS
EOS
$i = <<<'EOS'
EOS
$j = <<<"EOS"
EOS
$k = <<<EOS
EOS
$l = <<<EOS
EOS;

# just to check we're correctly out of a string here
$zzz_end = 42;
