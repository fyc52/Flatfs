#define TOTAL_DEPTH 8


char calculate_filename(char *name)
{
	int i = 0;
	char s;

	while ( name[i] && name[i] != '.' )
	{
		s ^= name[i];
		i++;
	}
	return s;
}

unsigned long calculate_part_lba(char s, int depth)
{
	unsigned long plba = 0x00000000UL;

	plba = plba + s << (TOTAL_DEPTH * 8 - depth * 8);
	return plba;
}

int parse_depth(unsigned long ino){
	int depth = 1;
	uint8_t mask1 = 255;//设定一级目录8bit

	if(!ino)
		return 0;//at root dir
	for(;;){
		if((ino & (mask1 << (TOTAL_DEPTH * 8 - depth * 8) )) == 0)
			break;
		depth++;
	}
	return depth;
}

unsigned long calculate_slba(struct inode* dir, struct dentry* dentry)
{
	char *name = dentry->d_name.name;
	unsigned long var = dir->i_ino;
	// struct dentry* tem_den = dir->i_dentry;
	// //是否为mount root？dentry对象的d_parent指针设置为指向自身是判断一个dentry对象是否是一个fs的根目录的唯一准则
	// if( tem_den->d_parent == tem_den ){
	// 	return 0x00000000UL;
	// } 
	unsigned long plba = calculate_part_lba(calculate_filename(name), (parse_depth(var)+1));
	var = var & plba;

	return var;
}