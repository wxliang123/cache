cd external
# lib
[ -d lib ] || mkdir lib

# ssmem
git clone https://github.com/LPD-EPFL/ssmem
cd ssmem
make libssmem.a
cp libssmem.a ../lib/
cp include/ssmem.h ../include/
cd ..

# sspfd
git clone https://github.com/trigonak/sspfd
cd sspfd
make
cp libsspfd.a ../lib/
cp sspfd.h ../include/
cd ..

cd ../
make libclht_lb_res.a