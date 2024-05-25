FROM ghcr.io/educelab/ci-docker:dynamic.12.0
MAINTAINER Seth Parker <c.seth.parker@uky.edu>

RUN apt-get update && apt-get upgrade -y 
RUN apt-get install -y libsdl2-dev libgsl-dev

# Install volcart
COPY ./ /volume-cartographer/
RUN export CMAKE_PREFIX_PATH="/usr/local/Qt-6.6.1/" \
    && cmake  \
      -S /volume-cartographer/ \
      -B /volume-cartographer/build/ \
      -GNinja  \
      -DCMAKE_BUILD_TYPE=Release  \
      -DCMAKE_INSTALL_RPATH=/usr/local/Qt-6.6.1/lib \
      -DSDL2_DIR=/usr/lib/x86_64-linux-gnu/ \
      -DVC_BUILD_ACVD=ON  \
    && cmake --build /volume-cartographer/build/ \
    && cmake --install /volume-cartographer/build/ \
    && rm -rf /volume-cartographer/

# Start an interactive shell
ENTRYPOINT export QT_PLUGIN_PATH=/usr/local/Qt-6.6.1/plugins && /bin/bash
