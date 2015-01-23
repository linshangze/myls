/*********************************************************************
 * File Name: lsz_ls_v2.c
 * Author: linshangze
 * Mail: 739314063@qq.com 
 * Created Time: 2014-05-21-08:16:09 AM
 ********************************************************************/

//////////////////////////////////////////////////////////////////////
//利用linux的系统编程实现ls工具。
//目前实现功能：
//选项：l，a，u，R。
//文件参数（无/结尾，），路径参数（以/结尾）。
//
//和系统ls的一些区别：
//在跟目录参数时，统一以“/”作为结尾，如“//”，“./”，
//否则认为是普通文件或目录文件。
//
//更新：
//增加u，R功能，修正l功能的显示格式和total数值，
//增加分栏输出，
//可以适应不同大小终端，不同长度文件，不同文件个数。
//使用更该一些字符串复制，连接，求长函数。
//修改某些函数的数据功能封装。
//修改一下代码风格。
//
//问题：
//采用动态数组，当打开大目录时，对空间的要求较高。
//在递归遍历根目录时，该目录/var/run/user/lsz/gvfs出现权限错误，
//无论用sudo还是切换到超级用户。
//root下看目录属性为：d????????? ? ?    ?      ?            ? gvfs/
//root没有进入查看权限。
//普通用户下看书属性为：dr-x------ 2 lsz lsz   0 Jun  1 09:13 gvfs
///./a.out usr/bin/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/
//stat: /usr/bin/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/X11/lzfgrep: Too many levels of symbolic links
//////////////////////////////////////////////////////////////////////

#include <stdio.h> 
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
//#include <Linux/limits.h>
#include <dirent.h>
#include <grp.h>
#include <pwd.h>
#include <errno.h>
#include <termio.h>

#define PARAM_NONE 0	//无参数
#define PARAM_A 1		//有参数a
#define PARAM_L 2		//有参数l
#define PARAM_U 4		//有参数u
#define PARAM_R 8		//有参数R

//向上取整。
#define DIVIDE_ROUDING_UP(dividend, divisor) \
	((dividend) / (divisor) + ((dividend) % (divisor) ? 1: 0))

void display_err(const char * err_string, int line);	//错误处理函数
int get_param(int argc, char *argv[]); //解读参数的函数
void display_file(const char *path, int param); //输出单个文件的函数
void display_directory(const char *path, int param); //输出目录里的文件的函数
void display_attribute(struct stat *stat_buf, char *name, int param);

int line_max = 80;	//定义一行中的最多输出的字符数
int line_leave;		//记录每个目录输出时，一行剩余可输出的字符数
int name_max;		//记录每个文件里长度最大的文件名的长度

int main(int argc,
			char **argv)
{
	int param; //记录参数信息
	int flag_path = 0; //命令有没有路径选项标志
	struct winsize wSize;

	param = get_param(argc, argv); //获取参数信息
	ioctl(STDIN_FILENO, TIOCGWINSZ, &wSize);
	line_max = wSize.ws_col;
	while(--argc != 0){ //检查路径选项
		++argv;
		if((*argv)[0] != '-'){ //非参数选项
			flag_path = 1;
			if((*argv)[strnlen(*argv, PATH_MAX) - 1] != '/'){ //没有“/”符号被当做文件。
				printf("the file %s:\n", *argv);
				display_file(*argv, param); //打印指定文件
			}else{
				display_directory(*argv, param); //打印指定目录
			}
		}
	}
	if(flag_path == 0){ //没有路径选项
		display_directory("./", param);
	}
	return 0;
}

/**
 * 输出程序执行错误信息并终结程序。
 *
 * @err_string：用户给出的错误信息提示。
 * @line：display_err被调用的代码行数。
 */
void display_err(const char const *err_string,
			int line)
{
	fprintf(stderr, "line: %d ", line);
	perror(err_string);
	exit(1);
}

/**
 * 获取所有的选项信息。
 *
 * @argc：参数个数。
 * @argv：参数字符串指针数组。
 */
int get_param(int argc,
				char *argv[])
{
	int param = PARAM_NONE;		//记录参数的信息
	int i;		//用于读解参数

	while(--argc != 0){ //读解命令
		++argv;		//解读选项
		if((*argv)[0] == '-'){ //命令中有参数选项
			for(i = 1; (*argv)[i] != '\0'; i++){
				if((*argv)[i] == 'a'){ //参数中有a
					param |= PARAM_A;
					continue;
				}else if((*argv)[i] == 'l'){ //参数中有l
					param |= PARAM_L;
					continue;
				}else if((*argv)[i] == 'u'){ //参数中有u
					param |= PARAM_U;
					continue;
				}else if((*argv)[i] == 'R'){ //参数中有R
					param |= PARAM_R;
					continue;
				}else{ //若程序到此，则出现无法识别参数，程序终止
					printf("%s: invalid option: %c\n", argv[0], (*argv)[i]);
					exit(1);
				}
			}
		}
	}
	return param;
}

/**
 * 提取同一目录下的文件名序列并分栏。
 *
 * @filePath：同一目录下的文件名数组。
 * @countFile：文件名数组的元素个数。
 * @lenField：用于返回存放分栏后的每栏长度，-1为结尾标志。
 */
void split_field(char (*filePath)[PATH_MAX + 1],
					int countFile,
					int lenField[])
{
	int posEnd, posBegin, posField; //文件序列的开始下标，结束下标，分栏后的栏下标。
	int *maxTable = NULL; //记录从posEnd到posBegin下标的文件最长长度。
	int posFileName; //文件名在路径中的开始下标。
	int offsetTable; //用一维数组模拟二维数组的下标偏移量。
	int sumLenField; //分栏后的总长度。
	int countColField; //分栏的列数。
	int countRowField; //分栏的行数。
	int lenCurFile; //当前文件名长度。

	if(countFile <= 0){
		return;
	}
	for(posFileName = strnlen(filePath[0], PATH_MAX + 1);
		posFileName > 0 && filePath[0][posFileName - 1] != '/'; posFileName--){
		; //获取文件名的开始位置。
	}
	if((maxTable = (int*)malloc( //申请一长度记录表。
		sizeof(int) * (((1 + countFile) * countFile) / 2))) == NULL){
		display_err("malloc", __LINE__);
	}
	for(posBegin = 0; posBegin < countFile; posBegin++){ //用动态规划求出长度记录表。
		for(posEnd = posBegin; posEnd < countFile; posEnd++){
			//offset这里表达不可以因式合并，为了防止不整除2。
			offsetTable = posBegin * countFile + posEnd - (posBegin + 1) * posBegin / 2;
			lenCurFile = strnlen(filePath[posEnd] + posFileName, PATH_MAX + 1);
			lenCurFile > line_max ? lenCurFile = line_max: 0; //修正超一行的文件名长度。
			if(posEnd > posBegin && maxTable[offsetTable - 1] > lenCurFile){
				maxTable[offsetTable] = maxTable[offsetTable - 1];
			}else{
				maxTable[offsetTable] = lenCurFile;
			}
		}
	}
	for(countColField = 1, sumLenField = 0;
		sumLenField < line_max && countFile >= countColField;
		countColField++){ //用超前尝试分栏列数。
		countRowField = DIVIDE_ROUDING_UP(countFile, countColField); //分栏后行数。
		for(posBegin = 0, posEnd = countRowField - 1, sumLenField = 0, posField = 0; //累加栏列宽度。
			posField < countColField;
			posBegin = posEnd + 1, posEnd += countRowField, posField++){
			posEnd > countFile - 1 ? posEnd = countFile - 1: 0;
			offsetTable = posBegin * countFile + posEnd - (posBegin + 1) * posBegin / 2;
			sumLenField += maxTable[offsetTable];
			if(posEnd == countFile - 1){
				break;
			}
			sumLenField += 2; //每个栏宽度间隔两个空格。
		}
		sumLenField -= 2; //栏尾不需要空格。
	}
	if(sumLenField > line_max && countColField > 2){ //要除掉只有1列的情况。
		countColField -= 2; //超前尝试分栏，第二重循环中，在分栏列数多加1前，列数宽已超出最大宽度。 
	}
	countRowField = DIVIDE_ROUDING_UP(countFile, countColField);
	for(posBegin = 0, posEnd = countRowField - 1, posField = 0;
		posField < countColField;
		posBegin = posEnd + 1, posEnd += countRowField, posField++){
		posEnd > countFile - 1 ? posEnd = countFile - 1 : 0;
		offsetTable = posBegin * countFile + posEnd - (posBegin + 1) * posBegin / 2;
		lenField[posField] = maxTable[offsetTable] + 2;
	}
	lenField[countColField--] = -1;
	lenField[countColField] -= 2; //栏尾不需要空格。
	free(maxTable);
}

/**
 * 对已分栏的同一目录下的文件名序列输出。
 *
 * @filePath：同一目录下的文件名数组。
 * @countFile：文件名数组的元素个数。
 * @lenField：用于返回存放分栏后的每栏长度，-1为结尾标志。
 */
void display_field(char (*filePath)[PATH_MAX + 1],
					int countFile,
					int lenField[])
{
	int countColFeild; //分栏列数。
	int countRowFeild; //分栏行数。
	int posFileName; //文件名在路径中的开始下标。
	int posColField; //当前栏的列下标。
	int posRowField; //当前栏的行下标。
	int posFile; //当前文件的下标。
	int blankLeave; //剩余的空格数。

	if(countFile <= 0){
		return;
	}
	for(countColFeild = 0; lenField[countColFeild] != -1; countColFeild++){
		;
	}
	countRowFeild = countFile / countColFeild;
	if(countFile % countColFeild){
		countRowFeild++;
	}
	for(posFileName = strlen(filePath[0]);
		posFileName > 0 && filePath[0][posFileName - 1] != '/'; posFileName--){
		; //获取文件名的开始位置。
	}
	for(posFile = posRowField = 0; posRowField < countRowFeild; posRowField++){
		for(posFile = posRowField, posColField = 0;
			posColField < countColFeild && posFile < countFile; posColField++){
			printf("%s", filePath[posFile] + posFileName);
			blankLeave = lenField[posColField] - strlen(filePath[posFile] + posFileName);
			while(blankLeave-- > 0){
				printf(" ");
			}
			posFile += countRowFeild;
		}
		printf("\n");
	}
}

/**
 * 打印单一带目录的文件及其属性。
 *
 * @path：带目录路径。
 * param：选项标志。
 */
void display_file(const char *path,
					int param)
{
	char name[PATH_MAX + 1];		//存放文件名
	int i;
	struct stat stat_buf;

	for(i = strlen(path) - 1; i > 0 && path[i - 1] != '/'; i--){
		; //获取文件名的开始位置。
	}
	strncpy(name, &path[i], PATH_MAX); //这样做是为了消除编译警告
	if((name[0] == '.') && ((param & PARAM_A) == 0)){ //若无参数a,则不输出隐藏文件
		return;
	}
	if(stat(path, &stat_buf) == -1)	//获取文件属性
		display_err(path, __LINE__);
	display_attribute(&stat_buf, name, param); //输出文件信息
}

/**
 * 补存路径，方便stat使用。
 *
 * stat函数对文件的路径要求完整。
 * 如果是相对路径，就必须以“.”或“..”开头；
 * 如果是绝对路径，就必须以“/”开头。
 *
 * @path：输入路径，带'\0'大小不超过PATH_MAX+1。
 * @path_ext[]：输出路径，带'\0'大小不超过PATH_MAX+1。
 */
void complete_path(char path_ext[], const char const path[])
{
	if((path[0] != '.' && path[0] != '/' && path[0] != '~' && path[0] != '-')
		|| (path[0] == '.' && path[1] == '.' && path[2] != '/')
			|| (path[0] == '.' && path[1] != '/')){
		strcpy(path_ext, "./"); //默认为当前路径。
		strncat(path_ext, path, PATH_MAX - strlen("./"));
	}else{
		strncpy(path_ext, path, PATH_MAX);
	}
}

/**
 * 打印一目录。
 *
 * @path：目录路径。
 * @param：选项标志。
 */
#define INCREASE_COUNT 16; //动态数组长度增量。
void display_directory(const char *path, int param)
{
	DIR *dir; //存放目录流
	struct dirent *ptr;	//存放目录项信息结构
	char path_ext[PATH_MAX + 1]; //用于补全绝对和相对路径。
	int size_record = 0;
	int count_record = 0; //目录下文件的总个数。
	int cur_record = 0;
	char (*filenames)[PATH_MAX + 1] = NULL;
	int total = 0; //目录下文件的总大小，k为单位。
	struct stat stat_buf, cmp_buf; //用于属性比较排序。

	int size_queue = 0; //当前目录队列的大小。
	int count_queue = 0; //当前目录里的目录名的个数。
	int cur_queue = 0; //当前子目录文件名的存放下标。
	char (*file_queue)[PATH_MAX + 1] = NULL;
	char (*point_realloc)[PATH_MAX + 1];
	char path_sub[PATH_MAX + 1];

	int lenField[line_max];

	int k;

	line_leave = line_max;
	name_max = 0;
	complete_path(path_ext, path);
	if((dir = opendir(path_ext)) == NULL){ //获得目录流
		display_err(path_ext, __LINE__);
	}
	while((ptr = readdir(dir)) != NULL){ //获取指向目录项信息的结构体的指针
		if((ptr->d_name)[0] == '.' && (param & PARAM_A) == 0){
			continue; //隐藏文件。
		}
		strncpy(path_sub, path_ext, PATH_MAX + 1);
		strncat(path_sub, ptr->d_name, PATH_MAX + 1 - strnlen(path_sub, PATH_MAX + 1));
		if(stat(path_sub, &stat_buf) == -1){
			display_err(path_sub, __LINE__);
		}
		total += stat_buf.st_blocks / 2; //记录目录下文件的总大小。
		count_record++;
		if(count_record > size_record){
			size_record += INCREASE_COUNT;
			if((point_realloc = realloc(
				filenames, size_record * sizeof(char) * (PATH_MAX + 1))) == NULL){
				display_err("realloc", __LINE__);
			}else{
				filenames = point_realloc;
			}
		}
		if((param & PARAM_R) && S_ISDIR(stat_buf.st_mode) &&
			strcmp(ptr->d_name, ".") != 0 && strcmp(ptr->d_name, "..") != 0){
			count_queue++;
			if(count_queue > size_queue){
				size_queue += INCREASE_COUNT;
				if((point_realloc = realloc(
					file_queue, size_queue * sizeof(char) * (PATH_MAX + 1))) == NULL){
					display_err("realloc", __LINE__);
				}else{
					file_queue = point_realloc;
				}
			}
			strncpy(file_queue[cur_queue], path_sub, PATH_MAX );
			strncat(file_queue[cur_queue], "/", PATH_MAX + 1 - strnlen(file_queue[cur_queue], PATH_MAX + 1));
			cur_queue++;
		}
		//利用直接排序对文件名排序，寻找适合的位置
		for(cur_record = 0;cur_record < count_record - 1; cur_record++){
			if(param & PARAM_U && !(param & PARAM_L)){
				if(stat(filenames[cur_record], &cmp_buf) < 0){
					display_err(filenames[cur_record], __LINE__);
				}
				if(difftime(cmp_buf.st_atime, stat_buf.st_atime) < 0){
					break;
				}
			}else{
				if(strcmp(path_sub, filenames[cur_record]) < 0){
					break;
				}
			}
		}
		if(cur_record == count_record - 1){ //若path_sub最大，则放到最后
			strncpy(filenames[cur_record], path_sub, PATH_MAX);
		}else{
			for(k = count_record - 1; k > cur_record; k--){ //将部分文件后移，为要插入位置腾空
				strcpy(filenames[k], filenames[k - 1]);
			}
			strcpy(filenames[cur_record], path_sub); //将文件名插入
		}
		name_max = (name_max > strlen(filenames[cur_record])) ?
			name_max : strlen(filenames[cur_record]); //记录最长文件名的长度
	}
	closedir(dir);
	if(param & PARAM_R){ //递归输出目录需要区分目录。
		printf("In the path: %s\n", path_ext);
	}
	if(!(param & PARAM_L)){
		split_field(filenames, count_record, lenField);
		display_field(filenames, count_record, lenField);
	}else{
		printf("count %d total %d\n", count_record, total);
		for(cur_record = 0; cur_record < count_record; cur_record++){ //用temp_name存放文件完整路径
			display_file(filenames[cur_record], param);
		}
	}
	free(filenames);
	if(param & PARAM_R){
		for(cur_queue = 0; cur_queue < count_queue; cur_queue++){
			printf("\n");
			display_directory(file_queue[cur_queue], param);
		}
	}
	free(file_queue);
}

/**
 * 打印一文件的属性。
 *
 * @stat_buf：文件属性结构体指针。
 * @name：文件名。
 * @param：选项标志。
 */
void display_attribute(struct stat *stat_buf, char *name, int param)
{
#define PRINT_  printf("-");
	/*输出文件类型*/
	mode_t mode_file = stat_buf->st_mode;
	if(S_ISLNK(mode_file)){
		printf("l");
	}else if(S_ISREG(mode_file)){
		printf("-");
	}else if(S_ISDIR(mode_file)){
		printf("d");
	}else if(S_ISCHR(mode_file)){
		printf("c");
	}else if(S_ISBLK(mode_file)){
		printf("b");
	}else if(S_ISFIFO(mode_file)){
		printf("f");
	}else if(S_ISSOCK(mode_file)){
		printf("s");
	}
	/*打印文件所有者的权限*/	
	if(S_IRUSR & mode_file){
		printf("r");
	}else{
		PRINT_
	}
	if(S_IWUSR & mode_file){
		printf("w");
	}else{
		PRINT_
	}
	if(S_ISUID & mode_file){
		if(S_IXOTH & mode_file){
			printf("s");
		}else{
			printf("S");
		}
	}else if(S_IXUSR & mode_file){
		printf("x");
	}else{
		PRINT_
	}
	/*打印与文件所有者同组用户对该文件的权限*/
	if(S_IRGRP & mode_file){
		printf("r");
	}else{
		PRINT_
	}
	if(S_IWGRP & mode_file){
		printf("w");
	}else{
		PRINT_
	}
	if(S_ISGID & mode_file){
		if(S_IXOTH & mode_file){
			printf("s");
		}else{
			printf("S");
		}
	}else if(S_IXGRP & mode_file){
		printf("x");
	}else{
		PRINT_
	}
	/*打印其他用户对该文件的权限*/
	if(S_IROTH & mode_file){
		printf("r");
	}else{
		PRINT_
	}
	if(S_IWOTH & mode_file){
		printf("w");
	}else{
		PRINT_
	}
	if(S_ISVTX & mode_file){
		printf("T");
	}else if(S_IXOTH & mode_file){
		printf("x");
	}else{
		PRINT_
	}
	printf("%2d", (int)stat_buf->st_nlink);		//打印文件的链接数
	/*根据uid与gid获取文件所有者的用户名与组名*/
	printf("%4s", getpwuid(stat_buf->st_uid)->pw_name);
	printf("%4s", getgrgid(stat_buf->st_gid)->gr_name);
	printf(" %6d", (int)stat_buf->st_size);		//打印文件的大小
	char buf_time[32];
	if((param & PARAM_L) && (param & PARAM_U)){
		strcpy(buf_time, ctime(&stat_buf->st_atime));
	}else{
		strcpy(buf_time, ctime(&stat_buf->st_mtime));
	}
	buf_time[strlen(buf_time) - 1] = '\0';
	printf(" %.12s", 4+buf_time);		//打印文件的时间
	printf(" %s\n", name);		//打印文件名
}

