/*	Copyright (c) 2013-2015
	REhints <info@rehints.com>
	All rights reserved.
	
	==============================================================================
	
	This file is part of HexRaysCodeXplorer

 	HexRaysCodeXplorer is free software: you can redistribute it and/or modify it
 	under the terms of the GNU General Public License as published by
 	the Free Software Foundation, either version 3 of the License, or
 	(at your option) any later version.

 	This program is distributed in the hope that it will be useful, but
 	WITHOUT ANY WARRANTY; without even the implied warranty of
 	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 	General Public License for more details.

 	You should have received a copy of the GNU General Public License
 	along with this program.  If not, see
 	<http://www.gnu.org/licenses/>.

	==============================================================================
*/

#include "Common.h"
#include "ObjectType.h"
#include <struct.hpp>

/*
	Representation of the reconstructed type
*/
struct type_reference {
	tinfo_t type;

	// offset of the referenced field by the helper, if any
	int hlpr_off;

	// size of the referenced field by the helper, if any
	int hlpr_size;

	// offset of the field after all checks
	int final_off;

	// size of the field after all checks
	int final_size;

	void idaapi init(cexpr_t *e);

	void idaapi update_hlpr(int off, int num);

	void idaapi update_type(cexpr_t *e);

	int idaapi update_offset(int offset);

	int idaapi update_size(int offset);

	int idaapi get_type_increment_val();

	int idaapi get_offset();

	int idaapi get_size();
};

void idaapi type_reference::init(cexpr_t *e) {
	type = e->type;
	hlpr_off = 0;
	final_off = 0;

	hlpr_size = 0;
	final_size = 0;
}

void idaapi type_reference::update_type(cexpr_t *e) {
	type = e->type;
}

int idaapi type_reference::get_type_increment_val() {
	if(type.is_ptr()) {
		ptr_type_data_t ptr_deets;
		if(type.get_ptr_details(&ptr_deets)) {
			return ptr_deets.obj_type.get_size();
		}
	} else if (type.is_array()) {
		return 1;
	} else {
		return 1;
	}
}

int idaapi type_reference::update_offset(int offset) {
	int update_factor = get_type_increment_val();
	final_off += update_factor * offset;

	return final_off;
}

int idaapi type_reference::update_size(int size) {
	final_size = size;
	return final_size;
}

int idaapi type_reference::get_offset()
{
	return final_off + hlpr_off;
}

int idaapi type_reference::get_size()
{
	if(hlpr_size != 0)
		return hlpr_size;
	else
		return final_size;
}

void idaapi type_reference::update_hlpr(int off, int num)
{
	hlpr_off = off;
	hlpr_size = num;
}

struct type_builder_t : public ctree_parentee_t
{
 	cexpr_t *highl_expr;

	char highl_expr_name[MAXSTR];

	struct struct_filed
	{
		int offset;
		int size;
	};

	std::vector<struct_filed> structure; 
	
	int idaapi visit_expr(cexpr_t *e);

	char * get_structure(char * name, char * buffer, int buffer_size);

	tid_t get_structure(char * name=NULL);

	int get_structure_size();

	bool idaapi check_memptr(struct_filed &str_fld);

	bool idaapi check_idx(struct_filed &str_fld);

	bool idaapi check_helper(citem_t *parent, int &offs, int &size);

	bool idaapi check_ptr(cexpr_t *e, struct_filed &str_fld);
};

int get_idx_type_size(cexpr_t *idx_expr)
{
	qstring *buf;
	idx_expr->type.print(buf);
	
	if(strstr(buf->c_str(), "char"))
		return 1;
	else if(strstr(buf->c_str(), "short"))
		return 2;
	else if(strstr(buf->c_str(), "int"))
		return 4;

	return 0;
}

bool idaapi type_builder_t::check_helper(citem_t *parent, int &off, int &num)
{
	if(parent->op == cot_call)
	{
		cexpr_t *expr_2 = (cexpr_t *)parent;
		if(!strcmp(get_ctype_name(expr_2->x->op), "helper"))
		{
			char buff[MAXSTR];
			expr_2->x->print1(buff, MAXSTR, NULL);
			tag_remove(buff, buff, 0);

			if(!strcmp(buff, "LOBYTE"))
			{
				num = 1;
				off = 0;
			}
			else if(!strcmp(buff, "HIBYTE") || !strcmp(buff, "BYTE3"))
			{
				num = 1;
				off = 3;
			}
			else if(!strcmp(buff, "BYTE1"))
			{
				num = 1;
				off = 1;
			}
			else if(!strcmp(buff, "BYTE2"))
			{
				num = 1;
				off = 2;
			}
			else if(!strcmp(buff, "LOWORD"))
			{
				num = 2;
				off = 0;
			}
			else if(!strcmp(buff, "HIWORD"))
			{
				num = 2;
				off = 2;
			}
			else
			{
				return false;
			}

			return true;
		}
	}

	return false;
}

bool idaapi type_builder_t::check_memptr(struct_filed &str_fld)
{
	// check if it has at least two parents
	if ( parents.size() > 2 )
	{
		citem_t *parent_1 = parents.back();

		// check if its parent is memptr
		if(parent_1->is_expr() && (parent_1->op == cot_memptr))
		{
			citem_t *parent_2 = parents[parents.size() - 2];
			citem_t *parent_3 = NULL;
			
			int num = 0;
			int off = 0;
			
			// check presence of the helper block
			bool bHelper = check_helper(parent_2, off, num);
			if(bHelper)
				parent_3 = parents[parents.size() - 3];
			else
				parent_3 = parent_2;

			if(parent_2->is_expr() && (parent_2->op == cot_asg))
			{
				cexpr_t *expr = (cexpr_t *)parent_1;

				if(bHelper)
				{
					str_fld.offset = expr->m + off;
					str_fld.size = num;
				}
				else
				{
					str_fld.offset = expr->m;
					str_fld.size = expr->ptrsize;
				}

				return true;
			}
		}
	}

	return false;
}

bool idaapi type_builder_t::check_ptr(cexpr_t *e, struct_filed &str_fld)
{
	type_reference referInfo;
	referInfo.init(e);

	qstring dbg_info;

	bool done = false;

	int par_size = parents.size();
	// check if it has at least three parents
	if ( par_size > 2 )
	{
		int offset = 0;
		int parent_idx = 1;

		int num = 0;
		int off = 0;

		for (size_t i = 0 ; i < parents.size() - 1 ; i ++) {
			citem_t *parent_i = parents[parents.size() - i - 1];

			// if its parent is addition 
			if(parent_i->is_expr() && (parent_i->op == cot_add))
			{
				cexpr_t *expr_2 = (cexpr_t *)parent_i;
				
				// get index_value
				char buff[MAXSTR];
				expr_2->y->print1(buff, MAXSTR, NULL);
				tag_remove(buff, buff, 0);
				offset = atoi(buff);

				referInfo.update_offset(offset);
			} else if(parent_i->is_expr() && (parent_i->op == cot_cast)) {
				referInfo.update_type((cexpr_t *)parent_i);
			} else if(parent_i->is_expr() && check_helper((cexpr_t *)parent_i, off, num)) {
				referInfo.update_hlpr(off, num);
			} else if(parent_i->is_expr() && (parent_i->op == cot_ptr)) {
				referInfo.update_size(((cexpr_t *)parent_i)->ptrsize);
				done = true;
				break;
			} else if(parent_i->is_expr() && (parent_i->op == cot_memptr)) {
				referInfo.update_offset(((cexpr_t *)parent_i)->m);
				referInfo.update_size(((cexpr_t *)parent_i)->ptrsize);
				done = true;
				break;
			} else if(parent_i->is_expr() && (parent_i->op == cot_asg)) {
				done = true;
				break;
			} else if(parent_i->is_expr() && (parent_i->op == cot_call)) {
				done = true;
				break;
			}
		}
	}

	if(done) {
		str_fld.offset = referInfo.get_offset();
		str_fld.size = referInfo.get_size();
		if (str_fld.size == 0) {
			str_fld.size = 4;
		}
	}

	return done;
}

bool idaapi type_builder_t::check_idx(struct_filed &str_fld)
{
	// check if it has at least two parents
	if ( parents.size() > 1 )
	{
		citem_t *parent_1 = parents.back();

		// if its parrent is 
		if(parent_1->is_expr() && (parent_1->op == cot_memptr))
		{
			citem_t *parent_2 = parents[parents.size() - 2];
			if(parent_2->op == cot_idx)
			{
				cexpr_t *expr_2 = (cexpr_t *)parent_2;
				
				// get index_value
				char buff[MAXSTR];
				expr_2->y->print1(buff, MAXSTR, NULL);
				tag_remove(buff, buff, 0);
				int num = atoi(buff);
						
				citem_t *parent_3 = parents[parents.size() - 3];
				if(parent_3->is_expr() && (parent_3->op == cot_asg))
				{
					cexpr_t *expr_1 = (cexpr_t *)parent_1;	
				
					str_fld.offset = expr_1->m + num;
					str_fld.size = get_idx_type_size(expr_2);

					return true;
				}
			}
		}
	}

	return false;
}

int idaapi type_builder_t::visit_expr(cexpr_t *e)
{
	// check if the expression being visited is variable
	if(e->op == cot_var)
	{
		// get the variable name
		char expr_name[MAXSTR];
		e->print1(expr_name, MAXSTR, NULL);
        tag_remove(expr_name, expr_name, 0);

		// check for the target variable
		if(!strcmp(expr_name, highl_expr_name))
		{
			struct_filed str_fld;

			if(check_ptr(e, str_fld))
				structure.push_back(str_fld);
		}
	}

	return 0;
}

int type_builder_t::get_structure_size()
{
	int highest_offset = 0;
	int reference_size = 0;

	
	for(std::vector<struct_filed>::iterator i = structure.begin(); i != structure.end() ; i ++)
	{
		if(highest_offset < i ->offset)
		{
			highest_offset = i ->offset;
			reference_size = i->size;
		}
	}

	return highest_offset + reference_size;
}

char * get_type_nm(int sz)
{
	switch(sz)
	{
	case 1:
		return "char";
	case 2:
		return "short";
	case 4:
		return "int";
	}

	return "unk";
}

void sort_fields(std::vector<type_builder_t::struct_filed> &un)
{
	for(unsigned int i = 0 ; i < un.size() ; i ++)
		for(unsigned int j = 0 ; j < un.size() - i - 1 ; j ++)
		{
			if(un[j].offset > un[j + 1].offset)
			{
				type_builder_t::struct_filed tmp = un[j];
				un[j] = un[j + 1];
				un[j + 1] = tmp;
			}
		}
}

char * type_builder_t::get_structure(char * name, char * bufferr, int buffer_size)
{
	sort_fields(structure);
	char *buffer = bufferr;
	int offs = 0;

	buffer += sprintf_s(buffer, buffer_size - (int)(buffer - bufferr), "struct %s {\r\n", name);
	for(unsigned int i = 0 ; i < structure.size() ; i ++)
	{
		if(structure[i].offset > offs)
		{
			buffer += sprintf_s(buffer, buffer_size - (int)(buffer - bufferr), "\\* 0x%X \\*\tchar\tfiller_%d[%d];\r\n", offs, i, structure[i].offset - offs);
			offs = structure[i].offset;
		}
		
		if(structure[i].offset == offs)
		{
			buffer += sprintf_s(buffer, buffer_size - (int)(buffer - bufferr), "\\* 0x%X \\*\t%s\tfield_%d;\r\n", offs, get_type_nm(structure[i].size), i);
			offs += structure[i].size;
		}
	}

	buffer += sprintf_s(buffer, buffer_size - (int)(buffer - bufferr), "}");

	return NULL;
}

tid_t type_builder_t::get_structure(char * name)
{
	tid_t struct_type_id = add_struc(BADADDR, name);
	if (struct_type_id != 0 || struct_type_id != -1)
	{
		struc_t * struc = get_struc(struct_type_id);
		if(struc != NULL)
		{
			sort_fields(structure);
			int offs = 0;
			opinfo_t opinfo;
			opinfo.tid = struct_type_id;
			
			for(unsigned int i = 0 ; i < structure.size() ; i ++)
			{
				if(structure[i].offset > offs)
				{
					offs = structure[i].offset;
				}
		
				flags_t member_flgs = 0;
				if(structure[i].size == 1)
					member_flgs = byteflag();
				else if (structure[i].size == 2)
					member_flgs = wordflag();
				else if (structure[i].size == 4)
					member_flgs = dwrdflag();
				else if (structure[i].size == 8)
					member_flgs = qwrdflag();

				char field_name[258];
				memset(field_name, 0x00, sizeof(field_name));
				sprintf_s(field_name, sizeof(field_name), "field_%d", i);

				int iRet = add_struc_member(struc, field_name, structure[i].offset, member_flgs, NULL, structure[i].size);
				offs += structure[i].size;
			}
		}
	}
	return struct_type_id;
}


bool idaapi reconstruct_type(void *ud)
{
	vdui_t &vu = *(vdui_t *)ud;
  
	// Determine the ctree item to highlight
	vu.get_current_item(USE_KEYBOARD);
	citem_t *highlight = vu.item.is_citem() ? vu.item.e : NULL;

	// highlight == NULL might happen if one chooses variable at local variables declaration statement
	if(highlight != NULL)
	{
		// the chosen item must be an expression and of 'variable' type
		if(highlight->is_expr() && (highlight->op == cot_var))
		{
			cexpr_t *highl_expr = (cexpr_t *)highlight;

			// initialize type rebuilder
			type_builder_t type_bldr;
			type_bldr.highl_expr = highl_expr;

			highl_expr->print1(type_bldr.highl_expr_name, MAXSTR, NULL);
			tag_remove(type_bldr.highl_expr_name, type_bldr.highl_expr_name, 0);
		
			// traverse the ctree structure
			type_bldr.apply_to(&vu.cfunc->body, NULL);


			tid_t struct_type_id = type_bldr.get_structure(NULL);
			if(struct_type_id != 0 || struct_type_id != -1)
			{
				qstring struct_name;
				struct_name = get_struc_name(struct_type_id);
				va_list va;
				va_end(va);
				char * type_name = vaskstr(0, struct_name.c_str(), "Enter type name", va);
				if(type_name != NULL)
				{
					set_struc_name(struct_type_id, type_name);

					// get the structure description
					char buffr[MAXSTR*10];
					type_bldr.get_structure(type_name, buffr, sizeof(buffr));
					msg("%s", buffr);
				}
			}
		}
	}
	else
	{
		msg("Invalid item is choosen");
	}

	return true;
}