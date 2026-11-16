package=libxkbcommon
$(package)_version=0.8.4
$(package)_download_path=https://xkbcommon.org/download
$(package)_file_name=$(package)-$($(package)_version).tar.xz
$(package)_sha256_hash=60ddcff932b7fd352752d51a5c4f04f3d0403230a584df9a2e0d5ed87c486c8b
$(package)_dependencies=libxcb xcb_proto xproto

define $(package)_set_vars
$(package)_config_opts=--disable-shared --enable-x11 --without-wayland --disable-docs
$(package)_config_opts+=--with-xkb-config-root=/usr/share/X11/xkb --with-x-locale-root=/usr/share/X11/locale
$(package)_config_opts_linux=--with-pic
$(package)_cflags=-Wno-error
endef

define $(package)_preprocess_cmds
  cp -f $(BASEDIR)/config.guess $(BASEDIR)/config.sub build-aux
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef
