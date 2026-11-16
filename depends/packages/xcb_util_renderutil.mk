package=xcb_util_renderutil
$(package)_version=0.3.9
$(package)_download_path=https://xcb.freedesktop.org/dist
$(package)_file_name=xcb-util-renderutil-$($(package)_version).tar.bz2
$(package)_sha256_hash=c6e97e48fb1286d6394dddb1c1732f00227c70bd1bedb7d1acabefdd340bea5b
$(package)_dependencies=libxcb xproto

define $(package)_set_vars
$(package)_config_opts=--disable-shared
$(package)_config_opts_linux=--with-pic
endef

define $(package)_preprocess_cmds
  cp -f $(BASEDIR)/config.guess $(BASEDIR)/config.sub .
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
