/***********************************
 * @author uichuan47
 * @since  2024.1.1
 * @brief  数据池 类实现
 * @date   2024.1.2
 ***********************************/

Mat::Mat(double _time = 10.0) : time_stamp(_time)
{
	// cout << "Mat construct" << endl;
}

Mat::Mat(const Mat& m)
{

	// cout << "Mat copy construct" << endl;
	this->time_stamp = m.time_stamp;
}

void Mat::show() const
{
	cout << "*********************, time = " << time_stamp << endl;
}




Dat::Dat(double _steer = 5.0, double _speed = 20.0, double _time = 10.0) : steer(_steer), speed(_speed), time_stamp(_time)
{

	// cout << "Dat construct" << endl;
}

Dat::Dat(const Dat& d)
{

	// cout << "Dat copy construct" << endl;
	this->steer = d.steer;
	this->speed = d.speed;
	this->time_stamp = d.time_stamp;
}

void Dat::show() const
{
	cout << "steer = " << steer << ", speed = " << speed << ", time = " << time_stamp << endl;
}



Dota::Dota(int _sign = Dota_Nul) : sign(_sign)
{
	// cout << "sign = " << sign << "construct" << endl;
	// cout << "default construct" << endl;
}

Dota::Dota(const Mat& m)
{
	// cout << "sign = " << sign << "Mat Dota" << endl;
	sign = Dota_Mat;
	mat = m;
}

Dota::Dota(const Dat& d)
{
	// cout << "sign = " << sign << "Mat Dota" << endl;
	sign = Dota_Dat;
	dat = d;
}

// 拷贝构造
Dota::Dota(const Dota& dtmp)
{
	// cout << "copy construct" << endl;
	this->sign = dtmp.sign;

	switch (sign)
	{
	case Dota_Mat:
		mat = dtmp.mat;
		break;
	case Dota_Dat:
		dat = dtmp.dat;
		break;
	default:
		break;
	}
}

Dota::~Dota()
{
}


void Dota::show() const
{
	// cout << "Dota show " << sign << endl;

	switch (sign)
	{
	case Dota_Mat:
		// cout << sign << endl;
		mat.show();
		break;
	case Dota_Dat:
		// cout << sign << endl;
		dat.show();
		break;
	case Dota_Nul:
		// cout << sign << endl;
		cout << "Invalid data" << endl;
		break;
	default:
		// cout << "default" << endl;
		break;
	}
}


Dota_Pool::Dota_Pool()
{
}

Dota_Pool::~Dota_Pool()
{
	this->clear();
	// cout << "The data pool is cleared. Procedure" << endl;
}


void Dota_Pool::push(Mat m)
{

	// 创建变量
	Dota dtmp(m);

	// 上锁
	dota_mutex.lock();

	dotabox.push_back(dtmp);
	// cout << "push mat"
	//      << " size = " << size() << " sign = " << dtmp.sign << endl;

	// 解锁
	dota_mutex.unlock();
}

void Dota_Pool::push(Dat d)
{

	// 创建变量
	Dota dtmp(d);

	// 上锁
	dota_mutex.lock();

	dotabox.push_back(dtmp);
	// cout << "push dat"
	//      << " size = " << size() << " sign = " << dtmp.sign << endl;

	// 解锁
	dota_mutex.unlock();
}

Dota_Pool::Dota pop()
{
	// 上锁
	dota_mutex.lock();

	cout << "pop dota" << endl;

	if (dotabox.empty())
	{
		cout << "empty pop" << endl;
		return Dota(-1);
	}

	Dota dtmp = dotabox.front();
	dotabox.pop_front();

	// 解锁
	dota_mutex.unlock();

	return dtmp;
}

// 判断数据池是否为空
bool Dota_Pool::empty() const
{
	return dotabox.empty();
}

// 返回当前数据池中元素个数
int Dota_Pool::size() const
{
	return dotabox.size();
}

// 清空数据池全部元素
void Dota_Pool::clear()
{
	dotabox.clear();
}