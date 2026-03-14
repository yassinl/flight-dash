## setup

## build

``sudo apt-get update && sudo apt-get install -y python3-dev cython3 && make clean && make build-python && sudo make install-python && python3 -c "import rgbmatrix; print('rgbmatrix import OK')"``

# matrix-agent

This project is meant to render flight information to the matrix led dashboard. 