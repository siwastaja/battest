#include <ctime>
#include <cerrno>
#include <termios.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <fcntl.h>
#include <stropts.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <fstream>
#include <iomanip>

using namespace std;

int kbhit()
{
	static const int STDIN = 0;
/*	static bool initialized = false;

	if(!initialized)
	{
		termios term;
		tcgetattr(STDIN, &term)
	}
*/
	int bytesWaiting;
	ioctl(STDIN, FIONREAD, &bytesWaiting);
	return bytesWaiting;
}

int set_interface_attribs(int fd, int speed, int parity, int should_block)
{
	struct termios tty;
	memset(&tty, 0, sizeof(tty));
	if(tcgetattr(fd, &tty) != 0)
	{
		cerr << "error " << errno << " from tcgetattr" << endl;
		return -1;
	}

	cfsetospeed(&tty, speed);
	cfsetispeed(&tty, speed);

	tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
	tty.c_iflag &= ~IGNBRK;
	tty.c_lflag = 0;
	tty.c_oflag = 0;
	tty.c_cc[VMIN] = 255; // should_block ? 1 : 0;
	tty.c_cc[VTIME] = 1; // 0.1s read timeout

	tty.c_iflag &= ~(IXON | IXOFF | IXANY);
	tty.c_cflag |= (CLOCAL | CREAD);
	tty.c_cflag &= ~(PARENB | PARODD);
	tty.c_cflag |= parity;
	tty.c_cflag &= ~CSTOPB;
	tty.c_cflag &= ~CRTSCTS;

	if(tcsetattr(fd, TCSANOW, &tty) != 0)
	{
		cerr << "error " << errno << " from tcsetattr" << endl;
		return -1;
	}
	return 0;
}

int wr(int fd, const char* buf)
{
	int len = 0;
	while(buf[len] != 0) len++;
	if(len == 0) return 0;
	write(fd, buf, len);
}

int clear_err(int fd)
{
	int cnt = 0;
	char buf[1000];
//	usleep(10000);
//	read(fd, buf, 1000);
	while(1)
	{
		wr(fd, "SYST:ERR?\r\n");
//		usleep(80000);
		buf[0] = 0; buf[1] = 0;
		int kak = read(fd, buf, 1000);
		buf[kak] = 0;
		if(buf[0] == '0' || buf[1] == '0')
			break;

		cerr << "err " << cnt << ": " << buf << endl;

		cnt++;
		if(cnt > 20)
			return 0;
	}
	return cnt;
}

#define MODE_DSCH 1
#define MODE_CHA  2

int main(int argc, char** argv)
{
	ifstream settings;
	settings.open("settings");

	char device[1000];
	settings.get(device, 1000, '\t');

	char csv_dir[1000];
	char log_dir[1000];
	char csv_delim;

	settings.ignore(1, '\t');
	settings.get(csv_dir, 1000, '\t');
	settings.ignore(1, '\t');
	settings.get(log_dir, 1000, '\t');
	settings.ignore(1, '\t');
	csv_delim = settings.get();

	int mode = MODE_CHA;
	double curr = 0.5;
	double end_volt = 4.2;
	double end_curr = 0.5;

	int mode_res = 0;
	double res_lo_curr = 0;
	double res_hi_curr = 0;
	double res_lo_mult = 0;
	double res_hi_mult = 0;

	int res_interval = 120;
	int res_t1 = 10;
	int res_t2 = 10;
	int res_t3 = 10;
	double r_start_v_limit_dsch = 3.2;
	double r_start_v_limit_cha  = 4.1;
	double v_min = 2.0;
	double v_max = 4.5;

	settings >> res_interval;
	settings >> res_t1;
	settings >> res_t2;
	settings >> res_t3;
	settings >> r_start_v_limit_dsch;
	settings >> r_start_v_limit_cha;
	settings >> res_lo_mult;
	settings >> res_hi_mult;
	settings >> v_min;
	settings >> v_max;

	if(!settings.good())
		{cerr << "Couldn't process settings file." << endl; return 1;}

	if(res_interval < 6 || res_interval > 1000)
		{cerr << "Invalid res_interval" << endl; return 1;}
	if(res_t1 < 2 || res_t1 > 360)
		{cerr << "Invalid res_t1" << endl; return 1;}
	if(res_t2 < 2 || res_t2 > 360)
		{cerr << "Invalid res_t2" << endl; return 1;}
	if(res_t3 < 2 || res_t3 > 360)
		{cerr << "Invalid res_t3" << endl; return 1;}
	if(res_t1 + res_t2 + res_t3 >= res_interval)
		{cerr << "res_t1+res_t2+res_t3 >= res_interval" << endl; return 1;}
	if(r_start_v_limit_dsch < 0.1 || r_start_v_limit_dsch > 20.0)
		{cerr << "Invalid r_start_v_limit_dsch" << endl; return 1;}
	if(r_start_v_limit_cha < 0.1 || r_start_v_limit_cha > 20.0)
		{cerr << "Invalid r_start_v_limit_cha" << endl; return 1;}
	if(res_lo_mult < 0.1 || res_lo_mult > 0.95)
		{cerr << "Invalid res_lo_mult" << endl; return 1;}
	if(res_hi_mult < 1.05 || res_hi_mult > 10.0)
		{cerr << "Invalid res_hi_mult" << endl; return 1;}
	if(v_min < 0.01 || v_min > 20.0)
		{cerr << "Invalid v_min" << endl; return 1;}
	if(v_max < 0.01 || v_max > 20.0)
		{cerr << "Invalid v_max" << endl; return 1;}


	char buf[5000];

	sprintf(buf, "%s/%s.log", log_dir, argv[1]);
	ofstream log(buf, ios::app);
	if(!log.good())
	{
		cerr << "Could not open " << buf << endl;
		return 1;
	}


	if(argc < 6)
	{
		cerr << "Usage: battest name d[r]|c[r] curr endvolt endcurr" << endl; 
		cerr << "Also check \"settings\" file" << endl;
		return 1;
	}

	int fd = open(device, O_RDWR | O_NOCTTY | O_SYNC);
	if(fd < 0)
	{
		cerr << "error " << errno << " opening " << device << ": " << strerror(errno) << endl;
		return 1;
	}

	if(argv[2][0] == 'd')
		mode = MODE_DSCH;
	else if(argv[2][0] == 'c')
		mode = MODE_CHA;
	else
	{
		cerr << "Unknown mode" << endl;
		return 1;
	}

	curr = atof(argv[3]);
	if(curr < 0.01 || curr > 5.0)
	{
		cerr << "Invalid curr" << endl;
	}

	end_volt = atof(argv[4]);
	if(end_volt < 0.5 || end_volt > 20.0)
	{
		cerr << "Invalid end_volt" << endl;
	}

	end_curr = atof(argv[5]);
	if(end_curr < -0.0 || end_curr > 5.0)
	{
		cerr << "Invalid end_curr" << endl;
	}

	bool end_when_hit = false;
	if(end_curr == curr || end_curr == 0.0)
	{
		log << "Info: CV phase will be skipped." << endl;
		end_when_hit = true;
	}

	if(argv[2][1] == 'r')
	{
		mode_res = 1;
		res_lo_curr = res_lo_mult*curr;
		res_hi_curr = res_hi_mult*curr;
		if(res_hi_curr > 5.0)
		{
			cerr << "Warning: Resistance measurement hi current > 5.0. Using alternative method." << endl;
			res_lo_curr = 0.5*curr;
			res_hi_curr = curr;
		}

	}


	set_interface_attribs(fd, B9600, 0, 1);

	log << clear_err(fd) << " errors cleared" << endl;

	sprintf(buf, "%s/%s.csv", csv_dir, argv[1]);
	ofstream csv(buf, ios::app);
	if(!csv.good())
	{
		cerr << "Could not open " << buf << endl;
		return 1;
	}


	// 4096 samples * 9E-5 s = 369 ms, plus processing plus data transf.
	wr(fd, "OUTP OFF\r\n");
	wr(fd, "SENS:SWE:POIN 4096\r\n");
	wr(fd, "SENS:SWE:TINT 4E-5\r\n"); // was 6E-5
	double set_volt = end_volt;
	if(end_when_hit && mode==MODE_CHA)
		set_volt += 0.010;
	if(end_when_hit && mode==MODE_DSCH)
		set_volt -= 0.010;
	sprintf(buf, "VOLT %f;CURR %f\r\n", set_volt, curr);
	wr(fd, buf);



	int start_time = (int)(time(0));
	int state = 0;
	int res_cnt = 0;
	int res_measuring = 0;
	int res_meas_cnt = 0;
	int prev_t = 0;
	double res_v[3];
	double res_a[3];
	double mah_cumul = 0;
	double wh_cumul = 0;

	double r_avg = 0.0;
	int r_avg_samples = 0;
	double v_avg = 0.0;
	int v_avg_samples = 0;
	double r_min = 999999.0;
	double r_max = 0.0;
	char prev_v_char = '-';
	double view_r_avg = 0.0;
	int view_r_samples = 0;

	while(1)
	{
		int t = (int)(time(0))-start_time;
		log << "t=" << t << " ";
		cout << "t=" << setw(5) << t << " ";

		csv << t << csv_delim;
		int dt = t-prev_t;
		prev_t = t;

		if(clear_err(fd))
		{
			log << "Errors cleared." << endl;
		}

		if(state == 0)
		{
			state = 1;
		}
		else if(state == 1)
		{
			state = 2;
		}
		else if(state == 2)
		{
			state = 3;
			wr(fd, "OUTP ON\r\n");
		}

		wr(fd, "MEAS:VOLT?\r\n");
		int bytes = 0;
		bytes = read(fd, buf, 1000);
		buf[bytes] = 0;
		double v = atof(buf);
		log << "V=" << v << " ";
		cout << "V=" << setprecision(4) << setw(5) << v << " ";
		csv << v << csv_delim;

		if((v < v_min) || (v > v_max))
		{
			cerr << "Voltage out of safe range." << endl;
			wr(fd, "OUTP OFF\r\n");
			sleep(1);
			return 3;
		}

		wr(fd, "MEAS:CURR?\r\n");
		bytes = 0;
		bytes = read(fd, buf, 1000);
		buf[bytes] = 0;
		double a = atof(buf);
		if(a < -0.05 && mode == MODE_CHA)
		{
			cerr << "Negative current while charging" << endl;
			wr(fd, "OUTP OFF\r\n");
			sleep(1);
			return 4;
		}
		if(a > 0.05 && mode == MODE_DSCH)
		{
			cerr << "Positive current while discharging" << endl;
			wr(fd, "OUTP OFF\r\n");
			sleep(1);
			return 4;
		}
		if(a < 0.0) a *= -1.0;
		log << "A=" << a << " ";
		cout << "A=" << setprecision(4) << setw(8) << a << " ";
		csv << a << csv_delim;

		double w = v * a;
		double as = a * dt;
		double ws = w * dt;

		mah_cumul += 1000.0 * as / (60.0*60.0);
		wh_cumul += ws / (60.0*60.0);

		v_avg += v;
		v_avg_samples++;

		log << "W=" << w << " ";
		cout << "W=" << setprecision(4) << setw(8) << w << " ";
		log << "mAh_cumul=" << mah_cumul << " ";
		cout << "mAh_cumul=" << setprecision(4) << setw(8) << mah_cumul << " ";
		csv << mah_cumul << csv_delim;
		log << "Wh_cumul=" << wh_cumul << " ";
		cout << "Wh_cumul=" << setprecision(4) << setw(8) << wh_cumul << " ";
		csv << wh_cumul << csv_delim;

		bool r_printed = false;
		if(state == 3)
		{
			if((!end_when_hit && a < end_curr) ||
			   (end_when_hit  &&
				((mode == MODE_CHA && v >= end_volt) ||
				(mode == MODE_DSCH && v <= end_volt))
			   ))
			{
				wr(fd, "OUTP OFF\r\n");
				log << endl << "END" << endl;
				cout << endl << "END" << endl;
				break;
			}

			if(mode_res)
			{
				res_cnt += dt;
				res_meas_cnt += dt;

				if(res_cnt >= res_interval &&
					((mode==MODE_DSCH && v > r_start_v_limit_dsch) ||
					(mode==MODE_CHA && v < r_start_v_limit_cha)))
				{
					log << " r_start ";
					res_cnt -= res_interval;
					res_measuring = 1;
					res_meas_cnt = 0;
					sprintf(buf, "CURR %f\r\n", res_lo_curr);
					wr(fd, buf);
				}

				if(res_measuring == 1 && res_meas_cnt == res_t1)
				{
					log << " r_2 ";
					res_measuring = 2;
					res_meas_cnt = 0;
					sprintf(buf, "CURR %f\r\n", res_hi_curr);
					wr(fd, buf);
					res_v[0] = v;
					res_a[0] = a;
				}
				else if(res_measuring == 2 && res_meas_cnt == res_t2)
				{
					log << " r_3 ";
					res_measuring = 3;
					res_meas_cnt = 0;
					sprintf(buf, "CURR %f\r\n", res_lo_curr);
					wr(fd, buf);
					res_v[1] = v;
					res_a[1] = a;

				}
				else if(res_measuring == 3 && res_meas_cnt == res_t3)
				{
					log << " r_end ";
					res_measuring = 0;
					sprintf(buf, "CURR %f\r\n", curr);
					wr(fd, buf);
					res_v[2] = v;
					res_a[2] = a;
					double r = ((res_v[0]+res_v[2])/2 - res_v[1]) / 
						   ((res_a[0]+res_a[2])/2 - res_a[1]);
					if(r < 0.0) r *= -1.0;
					log << "r=" << r << " ";
					view_r_avg += r;
					view_r_samples++;
					csv << r << csv_delim;
					r_avg += r;
					r_avg_samples++;
					if(r > r_max)
						r_max = r;
					if(r < r_min)
						r_min = r;
					r_printed = true;
				}
			}
		}

		sprintf(buf, "%f", v);
		char v_char = buf[2];

		if(v_char != prev_v_char && !r_printed && res_measuring==0)
		{

			if(view_r_samples > 0)
			{
				cout << "r=" << setprecision(3) << setw(4) << view_r_avg/(double)view_r_samples*1000.0;
				cout << " (" << view_r_samples << ")";
				view_r_samples = 0;
				view_r_avg = 0.0;
			}
			cout << endl;
		}
		else
			cout << "       \r" << flush;

		prev_v_char = v_char;

		if(!r_printed)
		{
			csv << csv_delim;
		}

		log << endl;
		csv << endl;

		int prev = (int)(time(0));
		while((int)(time(0)) == prev)
		{
			prev = (int)(time(0));
			usleep(10000);
		}

		if(kbhit())
		{
			char cmd;
			cin >> cmd;
			cin.ignore();
			if(cmd == 'q' || cmd == 'Q')
			{
				wr(fd, "OUTP OFF\r\n");
				log << endl << "ABORTED" << endl;
				cout << endl << "ABORTED" << endl;
				log.close();
				csv.close();
				close(fd);
				sleep(1);
				return 2;
			}
		}
	}

	close(fd);
	sleep(1);

	r_avg /= (double)r_avg_samples;
	v_avg /= (double)v_avg_samples;

	r_avg*=1000.0;
	r_min*=1000.0;
	r_max*=1000.0;

	ofstream summary("summary", ios::app);
	if(!summary.good())
	{
		cerr << "Could not open summary file." << endl;
		return 5;
	}

	summary << start_time << '\t' << argv[1] << '\t';
	if(mode == MODE_CHA)
		summary << "CHA";
	else
		summary << "DSCH";

	summary << '\t' << curr << '\t' << end_volt << '\t' << end_curr << '\t';
	summary << setprecision(5) << mah_cumul << '\t' << setprecision(4) << wh_cumul << '\t';
	if(mode_res && r_avg_samples > 0)
		summary << setprecision(4) << r_min << '\t' << setprecision(4) << r_avg << '\t' << setprecision(4) << r_max << '\t';
	else
		summary << "N/A\tN/A\tN/A\t";
	summary << setprecision(4) << v_avg << endl;

	summary.close();

	csv << endl;
	log << endl;
	csv.close();
	log.close();

	return 0;
}
