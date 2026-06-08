package=blst
$(package)_version=0.3.13
$(package)_download_path=https://github.com/supranational/blst/archive/refs/tags
$(package)_file_name=v$($(package)_version).tar.gz
$(package)_sha256_hash=89772cef338e93bc0348ae531462752906e8fa34738e38035308a7931dd2948f

define $(package)_set_vars
$(package)_build_opts= CC="$($(package)_cc)" CFLAGS="$($(package)_cflags) $($(package)_cppflags) -fPIC -D__BLST_PORTABLE__"
endef

define $(package)_config_cmds
endef

define $(package)_build_cmds
  $($(package)_build_opts) ./build.sh
endef

define $(package)_stage_cmds
  mkdir -p $($(package)_staging_prefix_dir)/lib && \
  mkdir -p $($(package)_staging_prefix_dir)/include && \
  cp libblst.a $($(package)_staging_prefix_dir)/lib/ && \
  cp bindings/blst.h bindings/blst.hpp bindings/blst_aux.h $($(package)_staging_prefix_dir)/include/
endef
