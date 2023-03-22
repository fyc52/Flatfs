For complile, just cmd:make
For run, just cmd:sudo ./run.sh
For test filesystem performance:
1.cd /usr/local/share/filebench/workload
2.five workload to test:varmail.f, oltp.f, fileserver.f, webserver.f, webproxy.f
3.test cmd:sudo filebench -f [workload], example:sudo filebench -f varmail.f
Finally, you can change workload params, just code [workload]