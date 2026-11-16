package=libxcb
$(package)_version=1.14
$(package)_download_path=https://xcb.freedesktop.org/dist
$(package)_file_name=$(package)-$($(package)_version).tar.xz
$(package)_sha256_hash=a55ed6db98d43469801262d81dc2572ed124edc3db31059d4e9916eb9f844c34
$(package)_dependencies=xcb_proto libXau xproto

define $(package)_set_vars
$(package)_config_opts=--disable-static --enable-xkb --enable-xinput
endef

define $(package)_preprocess_cmds
  cp -f $(BASEDIR)/config.guess $(BASEDIR)/config.sub build-aux && \
  sed "s/pthread-stubs//" -i configure
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

define $(package)_postprocess_cmds
  rm -rf share/man share/doc
endef
