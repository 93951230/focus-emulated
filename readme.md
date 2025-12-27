# Intro

![alt text](readme_file/image.png)

This is a emulated game (i.e. idea referenced) of another great game called "Focus".

This also is a university final project of "intro to programming I".

The thing is written with allegro 5.

Programming: Me + Another teammate
Visual & Design: Another teammate

# Sketch tutorial

Press arrow keys to move / select things

Hold A to focus, then you can use arrow key and releasing A key will teleport you to the selected position.

You can only teleport if you have a circle aroud you

![alt text](readme_file/image-1.png)

The circle indicates where you can teleport, and the radius represents your "focus" amount. When the circle is small, it becomes red, and then you've run out of the "focus". You can't focus if you run out of "focus". You can say:

> Radius of blue circle = amount of focus

![alt text](readme_file/image-2.png)

Press L to change between light mode / dark mode

Press D to toggle debug mode on / off

The goal of the game is to step on all target blocks. (The glowing lamp).

# Edit Level Tutorial

You can edit level yourselves.

First, open `lvl_config.txt`.
The default one looks like 

```
11
lvl_0.txt
lvl_1.txt
lvl_2.txt
lvl_3.txt
lvl_4.txt
lvl_5.txt
lvl_6.txt
lvl_7.txt
lvl_8.txt
lvl_9.txt
lvl_a.txt
```

Here, 11 means there are 11 levels and then the following 11 lines of code represents the file name of the level txt, for example, the game will load "level/lvl_0.txt", "level/lvl_1.txt", ... and so on.

The order matters, as it decides the order level player plays.

This file should be modified according to the actual content of the `level` file,
where all level txts is stored.

Take `lvl_6.txt` for example:

```
scale 2
tilemap
19 9
1111111111111111111
1000001090109010001
1000001000100010001
1000001000100010001
10p0001000100010001
1120001070107010701
1000001000100010001
1aaaaa1aaa1aaa1aaa1
1111111111111111111
focus 3
```

which represents this level: 

![alt text](readme_file/image-3.png)

And take 

```
scale 2
tilemap
14 10
11111111111111
10000000010001
10000000010001
10000000010001
10p00000010001
10G00000010001
10100000010701
10000000010001
1aaaaaaaa1aaa1
11111111111111
focus 3
focus_rate_dec 2
focus_rate_inc 1
```

which represents this level: 

![alt text](readme_file/image-4.png)

## `scale`

means how large the tilemap should be placed, as compared to the case `scale = 1`

when this is not specified in the txt file, it is defaulted to 1.

## `tilemap`

decides the shape, where
first two input is width `W` and height `H`
then there are `H` lines of code, each line should have `W` characters, where

* `0` is air
* `1` is brick/wall
* `2` is another type of brick
* `3` is half brick
* `7` is target block
* `8` is ice
* `9` is crystal
* `a` is spike
* `p` is position of player

* `A` to `G` are sign with fixed content, reserved for in-game tutorial purpose.

## `focus`

is the maximum teleporting distance (unit is per block).

That is, when focus is set to 3, then when the blue circle is largest, it will have radius = 3 block.

if this isn't set, `focus` is defaulted to 0, in which case the game will not render it at all.

## `focus_rate_inc` and `focus_rate_dec`

describes the speed in which your focus change. The unit is per second.

When you hold `A` (i.e. focus), the circle shrinks at given `focus_rate_dec` speed, and when you don't focus, the circle resumes size (until maximum given by `focus`) at a given `focus_rate_inc` speed.