set confirm off
target remote localhost:1234
add-symbol-file target 0xffffc00000000000
layout src
