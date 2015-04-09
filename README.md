# Real-Time DXT1/DXT5 compressor

Compressor by Dale Weiler [twitter](https://twitter.com/graphmaster)

DXT endpoint optimization algorithm by Shane Calimlim [twitter](https://twitter.com/ShaneCalimlim)

Color line algorithm by Fabian Giesen [twitter](https://twitter.com/rygorous)

### Resources
* https://raw.githubusercontent.com/nothings/stb/master/stb_dxt.h
* https://code.google.com/p/crunch/
* https://www.opengl.org/registry/specs/EXT/texture_compression_s3tc.txt
* https://code.google.com/p/libsquish/
* https://github.com/divVerent/s2tc/wiki

### Notes
#### DXT endpoint optimization
`#define DXT_OPTIMIZE` to enable DXT end point optimization. This helps with
old hardware fetches and improves on disk compression ratio.

`#define DXT_HIGHP` to enable use of `double` for color-line evaluation. This
can produce better results if the precision of `float` is just not cutting it.
Take note however that the final result will always be treated as `float`.

Change `kRefineIterations` for more color-line refinement iterations. This should
never be `< 1`. If you increase it `> 3` it's suggested you also enable `DXT_HIGHP`.

#### RYG covariance matrix
Uses RYG covariance matrix for standard derivation to establish color vector
line. This method works great except for a few boundary cases. Full green
next to full red will result in a covariance matrix like:
```
[  1, -1, 0 ]
[ -1,  1, 0 ]
[  0,  0, 0 ]
```

The power method for a starting vector can generate all zeros. So `[1, 1, 1]`
is not used in favor for `[1, 2.718281828, 3.141592654]` instead. If the power
method fails to find the largest eigenvector (which is incredibly rare) some
error will occur in the final result.
