#!/bin/bash

set +x
set -e
FFMPEG_SRC=`pwd`
BUILD_ROOT=`pwd`/../
FFMPEG_PREFIX=/usr/local/mt_vaapi/
GR_UMD_DIST=${BUILD_ROOT}/gr_umd_dist

die() {
    echo "$*" >&2
    exit 2
}

if [ ! -d "${BUILD_ROOT}/mt_video" ];then
	git clone git@sh-code.mthreads.com:sw/mt_video_release.git ${BUILD_ROOT}/mt_video
else
	git -C ${BUILD_ROOT}/mt_video pull
fi


mkdir -p ${GR_UMD_DIST}
cd ${GR_UMD_DIST}

GR_UMD_LATEST_TXT_URL="https://oss.mthreads.com/release-ci/gr-umd/develop/latest.txt"
GR_UMD_LATEST_TXT=${GR_UMD_DIST}/gr_umd_latest.txt
rm -f ${GR_UMD_LATEST_TXT}
wget -O ${GR_UMD_LATEST_TXT} ${GR_UMD_LATEST_TXT_URL}
if ! [ -f ${GR_UMD_LATEST_TXT} ]; then die "failed to get ${GR_UMD_LATEST_TXT_URL}"; fi


download_umd() {
    #x86_64, arm64, loongarch64
    local ARCH_NAME=$1  

    cd ${GR_UMD_DIST}
    GR_UMD_PKG_URL=`cat ${GR_UMD_LATEST_TXT}`_${ARCH_NAME}-mtgpu_linux-xorg-release-hw.tar.gz
    GR_UMD_PKG=`basename ${GR_UMD_PKG_URL}`
    if ! [ -f ${GR_UMD_PKG} ]; then wget -O ${GR_UMD_PKG} ${GR_UMD_PKG_URL}; fi
    if ! [ -f ${GR_UMD_PKG} ]; then die "failed to get ${GR_UMD_PKG_URL}"; fi
    rm -rf ${ARCH_NAME}-mtgpu_linux-xorg-release
    tar xvf ${GR_UMD_PKG}
}

gen_debian_postinst() {
	echo "# !/bin/sh"
	echo "ldconfig"
}

gen_debian_control() {
	#x86_64, arm64, loongarch64
	local ARCH_NAME=$1
	local FF_VER=$2

	if [[ "$ARCH_NAME" == "aarch64" ]]; then
		ARCH_NAME="arm64"
	elif [[ "$ARCH_NAME" == "x86_64" ]]; then
		ARCH_NAME="amd64"
	fi

	echo "Package: ffmpeg-mt-build"
	echo "Version: ${FF_VER}"
	echo "Section: free"
	echo "Priority: optional"
	echo "Depends:"
	echo "Suggests:"
	echo "Architecture: ${ARCH_NAME}"
	echo "Installed-Size:"
	echo "Maintainer: mthreads"
	echo "Provides: mthreads"
	echo "Description: mthreads build ffmpeg ${FF_VER}."
}

create_ffmpeg_deb() {
	#x86_64, arm64, loongarch64
	local ARCH_NAME=$1
	local FF_VER=$2

	TEMP_PACKAGE_DIR=${BUILD_ROOT}/ffmpeg_cross_build/deb_package_dir
	rm -rf ${TEMP_PACKAGE_DIR}
	mkdir -p ${TEMP_PACKAGE_DIR}/usr/local/
	cp -rf /usr/local/mt_vaapi  ${TEMP_PACKAGE_DIR}/usr/local/
	mkdir -p ${TEMP_PACKAGE_DIR}/DEBIAN/
	gen_debian_postinst ${ARCH_NAME} ${FF_VER} > ${TEMP_PACKAGE_DIR}/DEBIAN/postinst
	gen_debian_control ${ARCH_NAME} ${FF_VER} > ${TEMP_PACKAGE_DIR}/DEBIAN/control
	chmod 555 ${TEMP_PACKAGE_DIR}/DEBIAN/postinst
	mkdir -p ${BUILD_ROOT}/mt_video/mt_video_dist
	dpkg -b ${TEMP_PACKAGE_DIR}  ${BUILD_ROOT}/mt_video/mt_video_dist/ffmpeg_${ARCH_NAME}.deb
	rm -rf /usr/local/mt_vaapi/
	rm -rf ${TEMP_PACKAGE_DIR}
}

cross_build_x86_64() {
	local FF_VER=$1
	local VA_VER=$2
	rm -rf ${FFMPEG_PREFIX}
	BUILDING_DIR=${BUILD_ROOT}/ffmpeg_cross_build/x86_64
	rm -rf ${BUILDING_DIR}
	mkdir -p ${BUILDING_DIR}
	cd ${BUILDING_DIR}

	PKG_CONFIG_PATH=${BUILD_ROOT}/mt_video/x86_64/${VA_VER}/lib/pkgconfig/:${BUILD_ROOT}/mt_video/x86_64/${VA_VER}/lib/x86_64-linux-gnu/pkgconfig/ ${FFMPEG_SRC}/configure --extra-version="mt-${FF_VER}" --enable-cross-compile --cross-prefix=x86_64-linux-gnu- --target-os=linux --arch=amd64 --enable-libdav1d --enable-rpath --extra-cflags="-I${BUILD_ROOT}/gr_umd_dist/x86_64-mtgpu_linux-xorg-release/usr/local/include" --extra-ldflags="-ldrm -lX11 -lxcb -lXau -lXdmcp -lXfixes -lXext -lX11-xcb -lxcb-dri3 -L${BUILD_ROOT}/gr_umd_dist/x86_64-mtgpu_linux-xorg-release/usr/local/lib -Wl,-rpath-link ${BUILD_ROOT}/mt_video/libdrm/amd64" --prefix=${FFMPEG_PREFIX} --enable-shared --disable-static --pkg-config-flags=--define-variable=prefix=${BUILD_ROOT}/mt_video/x86_64/${VA_VER}
	make -j$nproc
	make install
	cp config.h ${FFMPEG_PREFIX}/ffmpeg_config.h
}

cross_build_arm64() {
	local FF_VER=$1
	local VA_VER=$2
	rm -rf ${FFMPEG_PREFIX}
	BUILDING_DIR=${BUILD_ROOT}/ffmpeg_cross_build/arm64
	rm -rf ${BUILDING_DIR}
	mkdir -p ${BUILDING_DIR}
	cd ${BUILDING_DIR}

	PKG_CONFIG_PATH=${BUILD_ROOT}/mt_video/arm64/${VA_VER}/lib/pkgconfig/ ${FFMPEG_SRC}/configure --extra-version="mt-${FF_VER}" --enable-libdav1d --enable-cross-compile --cross-prefix=aarch64-linux-gnu- --target-os=linux --arch=aarch64 --enable-armv8 --extra-cflags="-march=armv8-a -I${BUILD_ROOT}/gr_umd_dist/arm64-mtgpu_linux-xorg-release/usr/local/include" --extra-ldflags="-ldrm -lX11 -lxcb -lXau -lXdmcp -lXfixes -lXext -lX11-xcb -lxcb-dri3 -L${BUILD_ROOT}/mt_video/libdrm/arm64 -L${BUILD_ROOT}/gr_umd_dist/arm64-mtgpu_linux-xorg-release/usr/local/lib" --pkg-config=pkg-config --enable-shared --disable-static --pkg-config-flags=--define-variable=prefix=${BUILD_ROOT}/mt_video/arm64/${VA_VER} --prefix=${FFMPEG_PREFIX}
	make -j$nproc
	make install
	cp config.h ${FFMPEG_PREFIX}/ffmpeg_config.h
}

cross_build_loongarch64() {
	local FF_VER=$1
	local VA_VER=$2
	rm -rf ${FFMPEG_PREFIX}
	BUILDING_DIR=${BUILD_ROOT}/ffmpeg_cross_build/loongarch64
	rm -rf ${BUILDING_DIR}
	mkdir -p ${BUILDING_DIR}
	cd ${BUILDING_DIR}

	PKG_CONFIG_PATH=${BUILD_ROOT}/mt_video/loongarch64/${VA_VER}/lib/pkgconfig/ ${FFMPEG_SRC}/configure --extra-version="mt-${FF_VER}" --enable-cross-compile --cross-prefix=loongarch64-linux-gnu- --target-os=linux --arch=loongarch64 --pkg-config=pkg-config --enable-shared --disable-static --pkg-config-flags=--define-variable=prefix=${BUILD_ROOT}/mt_video/loongarch64/${VA_VER} --prefix=${FFMPEG_PREFIX} --extra-cflags="-I${BUILD_ROOT}/gr_umd_dist/loongarch64-mtgpu_linux-xorg-release/usr/local/include" --extra-ldflags="-ldrm -lX11 -lxcb -lXau -lXdmcp -lXfixes -lXext -ldl -lX11-xcb -lxcb-dri3 -L${BUILD_ROOT}/mt_video/libdrm/loongarch64 -L${BUILD_ROOT}/gr_umd_dist/loongarch64-mtgpu_linux-xorg-release/usr/local/lib"
	make -j$nproc
	make install
	cp config.h ${FFMPEG_PREFIX}/ffmpeg_config.h
}

##
## create mt_video_dist/ffmpeg_${FF_VER}_${ARCH_NAME}.deb
##
download_umd x86_64
cross_build_x86_64 7.0.2 2.20
create_ffmpeg_deb x86_64 7.0.2

download_umd arm64
cross_build_arm64 7.0.2 2.20
create_ffmpeg_deb arm64 7.0.2

download_umd loongarch64
cross_build_loongarch64 7.0.2 2.20
create_ffmpeg_deb loongarch64 7.0.2

cd ${FFMPEG_SRC}
rm -rf mt_video_dist
ln -s ${BUILD_ROOT}/mt_video/mt_video_dist ./mt_video_dist
