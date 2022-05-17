#define FLATFS_ROOT_I 2

/* This is an example of filesystem specific mount data that a file system might
   want to store.  FS per-superblock data varies widely and some fs do not
   require any information beyond the generic info which is already in
   struct super_block */
struct flatfs_info {
	ino_t next_ino;
	ino_t max_ino;
};

// LIGHTFS superblock info
struct flatfs_sb_info {
	// DB info
	DB_ENV *db_env;
	DB *data_db;
	DB *meta_db;
	DB *cache_db;
	unsigned s_nr_cpus;
	struct flatfs_info __percpu *s_lightfs_info;
};