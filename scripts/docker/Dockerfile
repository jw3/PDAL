FROM pdal/pdal:dependencies
MAINTAINER Howard Butler <howard@hobu.co>

ARG MATLAB_ENABLED="${MATLAB_ENABLED:-OFF}"

ENV CC gcc
ENV CXX g++

RUN apt-get update && apt-get install -y --fix-missing --no-install-recommends \
        libhpdf-dev \
        python-all-dev \
        python-numpy \
    libsqlite3-mod-spatialite \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --depth=1 https://github.com/PDAL/PDAL \
    && cd PDAL \
    && git checkout master \
    && mkdir build \
    && cd build \
    && cmake \
        -DBUILD_PLUGIN_CPD=ON \
        -DBUILD_PLUGIN_MBIO=ON \
        -DBUILD_PLUGIN_GREYHOUND=ON \
        -DBUILD_PLUGIN_HEXBIN=ON \
        -DBUILD_PLUGIN_ICEBRIDGE=ON \
        -DBUILD_PLUGIN_MRSID=ON \
        -DBUILD_PLUGIN_NITF=ON \
        -DBUILD_PLUGIN_OCI=OFF \
        -DBUILD_PLUGIN_PCL=ON \
        -DBUILD_PLUGIN_PGPOINTCLOUD=ON \
        -DBUILD_PLUGIN_SQLITE=ON \
        -DBUILD_PLUGIN_RIVLIB=OFF \
        -DBUILD_PLUGIN_PYTHON=ON \
        -DBUILD_PLUGIN_MATLAB=${MATLAB_ENABLED} \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DENABLE_CTEST=OFF \
        -DWITH_APPS=ON \
        -DWITH_LAZPERF=ON \
        -DWITH_LASZIP=ON \
        -DWITH_TESTS=ON \
        -DWITH_PDAL_JNI=ON \
        -DCMAKE_BUILD_TYPE=Release \
        .. \
    && make -j 2\
    && make install

RUN pip install packaging \
    && pip install PDAL

RUN git clone https://github.com/PDAL/PRC.git \
    && cd PRC \
    && git checkout master \
    && mkdir build \
    && cd build \
    && echo `pwd` \
    && ls .. \
    && cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DPDAL_DIR=/usr/lib/pdal/cmake \
        -DCMAKE_INSTALL_PREFIX=/usr \
        .. \
    && make \
    && make install
