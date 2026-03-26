cp prtos_config.x86 prtos_config
make defconfig
make
make distro-run
./prtos-1.0.0.run < scripts/input.txt
cp scripts/run_bail_test.sh /home/${USER}/prtos-sdk
cd /home/${USER}/prtos-sdk
bash run_bail_test.sh --arch x86 check-all
