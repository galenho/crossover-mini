centos7.6��libstdc++�汾���͵Ľ���취��

(1) ��crossover/bin/libstdc++.so.6.0.21���Ƶ�/usr/lib64Ŀ¼
(2) cd /usr/lib64
	rm libstdc++.so.6
	ln -s libstdc++.so.6.0.21 libstdc++.so.6

	strings /usr/lib64/libstdc++.so.6 | grep GLIBC