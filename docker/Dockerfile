FROM centos:7

ENV LANG=en_US.UTF-8 \
    LC_ALL=en_US.UTF-8 \
    DEBIAN_FRONTEND=noninteractive \
    VCPKG_ROOT=/vcpkg \
    ORTOOLS_DIR=ortools

CMD ["/bin/bash"]

RUN mkdir /workspace

# Install requirements : update repo
RUN sed -i s/mirror.centos.org/vault.centos.org/g /etc/yum.repos.d/*.repo &&\
    sed -i s/^#.*baseurl=http/baseurl=http/g /etc/yum.repos.d/*.repo &&\
    sed -i s/^mirrorlist=http/#mirrorlist=http/g /etc/yum.repos.d/*.repo &&\
    yum install -y epel-release &&\
    yum install -y git redhat-lsb-core make wget centos-release-scl scl-utils python3 &&\
    sed -i s/mirror.centos.org/vault.centos.org/g /etc/yum.repos.d/*.repo &&\
    sed -i s/^#.*baseurl=http/baseurl=http/g /etc/yum.repos.d/*.repo &&\
    sed -i s/^mirrorlist=http/#mirrorlist=http/g /etc/yum.repos.d/*.repo

RUN  yum update -y
RUN  yum install -y epel-release
RUN  yum install -y git redhat-lsb-core make wget centos-release-scl scl-utils rpm-build && \
                yum install -y devtoolset-11 &&  \
                yum install -y rh-git227-git ccache
RUN  yum install -y unzip libuuid-devel wxGTK3-devel
RUN  yum install -y python3-pip &&    python3 -m pip install --upgrade pip &&    pip3 install pytest numpy pandas

# Install requirements
RUN rm -rf /var/cache/yum

RUN echo "source /opt/rh/devtoolset-11/enable" >> /etc/bashrc
SHELL ["/bin/bash", "--login", "-c"]

# Install CMake
RUN pip3 install cmake==3.28.4



ARG BRANCH=develop
RUN cd /workspace && \
    git clone https://github.com/BAZ-C/Antares_Simulator.git --branch $BRANCH && \
    cd Antares_Simulator && \
    git submodule update --init vcpkg && ./vcpkg/bootstrap-vcpkg.sh -disableMetrics && \
    export VCPKG_ROOT=/workspace/Antares_Simulator/vcpkg && \
    export ORTOOLS_TAG=$(cat  ortools_tag)

ENV ORTOOLS_TAG=$ORTOOLS_TAG


RUN URL_ORTOOLS=https://github.com/rte-france/or-tools-rte/releases/download/$ORTOOLS_TAG/ortools_cxx_centos7_static_sirius.zip && \
    mkdir -p ortools && cd ortools && \
            wget -q -O ortools.zip $URL_ORTOOLS && \
            unzip -q ortools.zip && \
            rm ortools.zip \

WORKDIR /workspace/Antares_Simulator

RUN  source /opt/rh/devtoolset-11/enable && \
     source /opt/rh/rh-git227/enable && \
     cmake -B _build -S src \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_TOOLCHAIN_FILE=/workspace/Antares_Simulator/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_TARGET_TRIPLET=x64-linux-release \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DBUILD_TOOLS=ON \
    -DBUILD_UI=OFF \
    -DCMAKE_PREFIX_PATH=/ortools/install

ARG NPROC=2
RUN  source /opt/rh/devtoolset-11/enable && \
     source /opt/rh/rh-git227/enable && \
     cmake --build _build --config Release -j${NPROC} &&\
     ccache -s

#Installer .rpm creation
RUN cd _build && \
    cpack -G RPM

#Solver archive creation
RUN cd _build && \
     cpack -G RPM

RUN cd _build && \
    cmake --install . --prefix install && \
    pushd . && \
    cd install/bin && \
    tar czf ../../antares-solver_centos7.tar.gz antares-solver libsirius_solver.so && \
    popd && \
    rm -rf install

#.tar.gz creation
RUN cd _build && \
    cpack -G TGZ
