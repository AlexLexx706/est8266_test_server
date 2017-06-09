#include "controller.h"
#include "assert.h"

#ifndef TEST_CONTROLLER
	#include "user_interface.h"
	#include "osapi.h"
#else 
	#define LOCAL static
	#define ICACHE_FLASH_ATTR
	#define os_strcmp strcmp
	#define os_strncmp strncmp
	#define os_strlen strlen
	#define os_sprintf sprintf
	#include <stdio.h>
	#include <string.h>
#endif

LOCAL GrilTreeItem * root_gril_item = NULL;

LOCAL GrilTreeItem * find_gril_tree_item(
	GrilTreeItem * head_item, const char * path);

LOCAL enum GrilStreamCmdParcerError root_set(
	struct GrilTreeItem_T * item, const char * value);

LOCAL enum GrilStreamCmdParcerError print_tree(
	struct GrilTreeItem_T * item, char ** out_buffer, const char * out_buffer_end);


void ICACHE_FLASH_ATTR
controller_init(GrilTreeItem * root) {
	assert(root);
	#ifdef DEBUG_CONTROLLER
	fprintf(stderr, "%s root:%p\n",  __FUNCTION__, root);
	#endif

	root_gril_item = root;
	tree_init(&root->tree);
	root->name = "/";
	root->fun_set = root_set;
	root->fun_print = print_tree;
}

LOCAL enum GrilStreamCmdParcerError ICACHE_FLASH_ATTR
root_set(struct GrilTreeItem_T * item, const char * value) {
	#ifdef DEBUG_CONTROLLER
	fprintf(stderr, "%s item->name:%s value:%s\n",  __FUNCTION__, item->name, value);
	#endif
	return GrilStreamCmdParcerNoError;
}

LOCAL enum GrilStreamCmdParcerError ICACHE_FLASH_ATTR
print_tree(struct GrilTreeItem_T * head, char ** out_buffer, const char * out_buffer_end) {
	assert(item);
	assert(out_buffer);
	assert(out_buffer_size);
	List * pos;
	GrilTreeItem * item;
	int str_len;
	enum GrilStreamCmdParcerError tmp_res;

	//short out buffer
	if ((*out_buffer) + 1 >= out_buffer_end)
		return GrilStreamCmdParcerErrorDataTooLong;

	*((*out_buffer)++) = '{';

	LIST_ITER(&(head->tree.child_head)) {
		item = LIST_ITEM(GrilTreeItem, tree.head, pos);
		if ((str_len = strlen(item->name)) >= (out_buffer_end - (*out_buffer)))
			return GrilStreamCmdParcerErrorDataTooLong;

		(*out_buffer) += strcpy((*out_buffer), item->name) + str_len;

		if ((*out_buffer) + 1 >= out_buffer_end)
			return GrilStreamCmdParcerErrorDataTooLong;
		((*out_buffer)++) = '=';

		//try print item value to out_buffer
		if (item->fun_print) {
			if ((tmp_res = item->fun_print(out_buffer, out_buffer_end)) != GrilStreamCmdParcerNoError)
				return tmp_res;
		} else {
			
		}
	}
	return GrilStreamCmdParcerNoError;
}


LOCAL GrilTreeItem * ICACHE_FLASH_ATTR
find_gril_tree_item(GrilTreeItem * head_item, const char * path) {
	List * pos;
	GrilTreeItem * item;
	assert(path);
	assert(head_item);
	#ifdef DEBUG_CONTROLLER
	fprintf(stderr, "%s item:%s path:%s\n",
		__FUNCTION__, head_item->name, path);
	#endif

	const char * end = strchr(path, '/');


	//has childs
	if (end) {
		int name_len = end - path;
		LIST_ITER(&(head_item->tree.child_head)) {
			item = LIST_ITEM(GrilTreeItem, tree.head, pos);

			if (!strncmp(item->name, path, name_len)) {
				return find_gril_tree_item(item, end + 1);
			}
		}
		return NULL;
	//no childs
	} else {
		LIST_ITER(&(head_item->tree.child_head)) {
			item = LIST_ITEM(GrilTreeItem, tree.head, pos);
			if (!strcmp(item->name, path)) {
				return item;
			}
		}
		return NULL;
	}
}

LOCAL void ICACHE_FLASH_ATTR controller_send_res(
		Userdata * user_data,
		enum GrilStreamCmdParcerError error,
		const char * prefix,
		const char * res_buffer) {
	int len = os_strlen(prefix) + 2;
	char out_buffer[120];

	if (error == GrilStreamCmdParcerNoError) {
		len += os_strlen(res_buffer);
		os_sprintf(out_buffer, "RE%03X%%%s%%%s\n", len, prefix, res_buffer);
		user_data->send_data(user_data->socket, out_buffer);
	} else {
		char error_buffer[32];
		os_sprintf(error_buffer, "error:%d", GrilStreamCmdParcerNoError);
		len += os_strlen(error_buffer);
		os_sprintf(out_buffer, "ER%03X%%%s%%%s\n", len, prefix, res_buffer);
		user_data->send_data(user_data->socket, out_buffer);
	}
}

void ICACHE_FLASH_ATTR
controller_process_commands(
		enum GrilStreamCmdParcerError error,
		const char * prefix,
		const char * cmd,
		const char * path,
		const char * value,
		void * _user_data) {
	assert(root_gril_item != NULL);
	char buffer[56]= {0};
	Userdata * user_data = (Userdata *)(_user_data);

	#ifdef DEBUG_CONTROLLER
	fprintf(stderr, "%s error:%d prefix:%s cmd:%s path:%s value:%s\n",
		__FUNCTION__, error,  prefix, cmd, path, value);
	#endif

	if (error == GrilStreamCmdParcerNoError) {
		int path_len = os_strlen(path);

		if (path_len > 0) {
			GrilTreeItem * item = (path_len == 1 && path[0] == '/') ?
				root_gril_item : find_gril_tree_item(
					root_gril_item, path[0] == '/' ? &path[1]: path); 

			if (item) {
				#ifdef DEBUG_CONTROLLER
				fprintf(stderr, "%s find item:%s\n",
					__FUNCTION__, item->name);
				#endif

				if (!os_strcmp(cmd, "set") && item->fun_set) {
					error = item->fun_set(item, value);
				} else if (!os_strcmp(cmd, "print") && item->fun_print) {
					error = item->fun_print(item, buffer, sizeof(buffer));
				} else {
					error = GrilStreamCmdParcerErrorUnknownCmd;
				}
			} else {
				error = GrilStreamCmdParcerErrorWrongParam;
			}
		}
	}
	controller_send_res(user_data, error, prefix, buffer);
}



#ifdef TEST_CONTROLLER
void send_data(void *socket, char *string) {
	fprintf(stderr, "%s res:%s\n", __FUNCTION__, string);
}

int main() {
	GrilTreeItem root_gril;
	GrilStreamCmdParcer parcer;
	GrilCommandNameDesc cmd_names[] = {{"print", 5}, {"set", 3}};
	Userdata user_data = {0, send_data};
	char test_date[] = "%%print,/,on\npri\nset,/,12\n%12%\n";

	controller_init(&root_gril);
	gril_stream_cmd_parcer_init(&parcer, controller_process_commands, cmd_names, 2);
	gril_stream_cmd_parcer_parce(&parcer, test_date, sizeof(test_date), &user_data);
	fprintf(stderr, "%s handler:%p\n", __FUNCTION__, controller_process_commands);
}
#endif