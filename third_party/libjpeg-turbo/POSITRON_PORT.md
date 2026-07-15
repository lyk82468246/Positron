# Positron libjpeg-turbo Port

- Upstream: https://github.com/libjpeg-turbo/libjpeg-turbo
- Version: 1.5.3
- Source commit: `bf6c774305c9feb30cff7b99e1a475df61bfa008`
- License: IJG, modified BSD and zlib terms in `LICENSE.md` and `README.ijg`

`positron_libjpeg.vcproj` builds only the generic C compressor subset for
Windows Mobile 6 ARMv4i. SIMD, arithmetic coding, command-line tools and the
decoder are not linked into `positron_image.dll`. The retained source image is
decoded by WM Imaging, exposed as locked 24bpp rows, and compressed with the
libjpeg API using explicit 4:4:4 sampling.

The upstream C sources are unchanged. `jconfig.h` and `jconfigint.h` are
generated platform configuration files. `positron_jpeg_compat.h` supplies the
missing VS2008/CE `SIZE_MAX` definition and disables desktop-only `getenv`
configuration through the project settings.

Binary documentation must retain the IJG attribution required by the upstream
license: "This software is based in part on the work of the Independent JPEG
Group."
