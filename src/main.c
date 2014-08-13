/*
 ============================================================================
 Name        : main.c
 Author      : sirius
 Copyright   : published under GPL
 Description : EPK2 firmware extractor for LG Electronic digital TVs
 ============================================================================
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <getopt.h>
#ifdef __CYGWIN__
#include <sys/cygwin.h>
#endif
#include <epk1.h>
#include <epk2.h>
#include <symfile.h>
#include <formats.h>

char exe_dir[1024];
char *current_dir;
int endianswap;

struct config_opts_t config_opts;

int handle_file(const char *file, struct config_opts_t *config_opts) {
	const char *dest_dir = config_opts->dest_dir;
	const char *file_name = basename(strdup(file));

	char dest_file[1024] = "";
	char lz4pack[1024] = "";

	if (check_lzo_header(file)) {
		constructPath(dest_file, dest_dir, file_name, ".lzounpack");
		printf("Extracting LZO file to: %s\n", dest_file);
		if (lzo_unpack(file, dest_file) == 0) {
			handle_file(dest_file, config_opts);
			return EXIT_SUCCESS;
		}
	} else if (is_nfsb(file)) {
		constructPath(dest_file, dest_dir, file_name, ".unnfsb");
		printf("Extracting nfsb image to: %s.\n\n", dest_file);
		unnfsb(file, dest_file);
		handle_file(dest_file, config_opts);
		return EXIT_SUCCESS;
	} else if (is_lz4(file)) {
		constructPath(dest_file, dest_dir, file_name, ".unlz4");
		printf("UnLZ4 file to: %s\n", dest_file);
		decode_file(file, dest_file);
		return EXIT_SUCCESS;			
	} else if (is_squashfs(file)) {
		constructPath(dest_file, dest_dir, file_name, ".unsquashfs");
		printf("Unsquashfs file to: %s\n", dest_file);
		rmrf(dest_file);
		unsquashfs(file, dest_file);
		return EXIT_SUCCESS;
	} else if (is_gzip(file)) {
		constructPath(dest_file, dest_dir, "", "");
		printf("Ungzip %s to folder %s\n", file, dest_file);
		strcpy(dest_file, file_uncompress_origname((char *)file, dest_file));
		if (dest_file) 
		    handle_file(dest_file, config_opts);
		return EXIT_SUCCESS;
	} else if(is_cramfs_image(file, "be")) {
		constructPath(dest_file, dest_dir, file_name, ".cramswap");
		printf("Swapping cramfs endian for file %s\n", file);
		cramswap(file, dest_file);
		handle_file(dest_file, config_opts);
		return EXIT_SUCCESS;
	} else if(is_cramfs_image(file, "le")) {
		constructPath(dest_file, dest_dir, file_name, ".uncramfs");
		printf("Uncramfs %s to folder %s\n", file, dest_file);
		rmrf(dest_file);
		uncramfs(dest_file, file);
		return EXIT_SUCCESS;
	} else if (isFileEPK2(file)) {
		extractEPK2file(file, config_opts);
		return EXIT_SUCCESS;
	} else if (isFileEPK3(file)) {
		extractEPK3file(file, config_opts);
		return EXIT_SUCCESS;
	} else if (isFileEPK1(file)) {
		extract_epk1_file(file, config_opts);
		return EXIT_SUCCESS;
	} else if (is_kernel(file)) {
		constructPath(dest_file, dest_dir, file_name, ".unpaked");
		printf("Extracting boot image to: %s.\n\n", dest_file);
		extract_kernel(file, dest_file);
		handle_file(dest_file, config_opts);
		return EXIT_SUCCESS;
	} else if(isPartPakfile(file)) {
		constructPath(dest_file, dest_dir, remove_ext(file_name), ".txt");
		printf("Saving Partition info to: %s\n", dest_file);
		dump_partinfo(file, dest_file);
		return EXIT_SUCCESS;
	} else if(is_jffs2(file)) {
		constructPath(dest_file, dest_dir, file_name, ".unjffs2");
		printf("jffs2extract %s to folder %s\n", file, dest_file);
		rmrf(dest_file);
		jffs2extract(file, dest_file, "1234");
		return EXIT_SUCCESS;
	} else if(isSTRfile(file)) {
		constructPath(dest_file, dest_dir, file_name, ".ts");
		setKey();
		printf("\nConverting %s file to TS: %s\n", file, dest_file);
		convertSTR2TS(file, dest_file, 0);
		return EXIT_SUCCESS;
	} else if(!memcmp(&file[strlen(file)-3], "PIF", 3)) {
		constructPath(dest_file, dest_dir, file_name, ".ts");
		setKey();
		printf("\nProcessing PIF file: %s\n", file);
		processPIF(file, dest_file);
		return EXIT_SUCCESS;
	} else if(symfile_load(file) == 0) {
		constructPath(dest_file, dest_dir, file_name, ".idc");
		printf("Converting SYM file to IDC script: %s\n", dest_file);
		symfile_write_idc(dest_file);
		return EXIT_SUCCESS;
	}
	return EXIT_FAILURE;
}

void ARMThumb_Convert(unsigned char* data, uint32_t size, uint32_t nowPos, int encoding) {
	uint32_t i;
	for (i = 0; i + 4 <= size; i += 2) {
		if ((data[i + 1] & 0xF8) == 0xF0 && (data[i + 3] & 0xF8) == 0xF8) {
			uint32_t src = ((data[i + 1] & 0x7) << 19) | (data[i + 0] << 11) | ((data[i + 3] & 0x7) << 8) | (data[i + 2]);
			src <<= 1;
			uint32_t dest;
			if (encoding)
				dest = nowPos + i + 4 + src;
			else
				dest = src - (nowPos + i + 4);
			dest >>= 1;
			data[i + 1] = 0xF0 | ((dest >> 19) & 0x7);
			data[i + 0] = (dest >> 11);
			data[i + 3] = 0xF8 | ((dest >> 8) & 0x7);
			data[i + 2] = (dest);
			i += 2;
		}
	}
}

#define N		 4096
#define F		   34
#define THRESHOLD	2
#define NIL			N

unsigned long int textsize = 0, codesize = 0;
unsigned char text_buf[N + F - 1];
int	match_length, match_position, lson[N + 1], rson[N + 257], dad[N + 1];

void InitTree(void) { 
	int  i;
	for (i = N + 1; i <= N + 256; i++) rson[i] = NIL;
	for (i = 0; i < N; i++) dad[i] = NIL;
}

void lazy_match(int r) {
	unsigned char *key;
	unsigned i, p, tmp;
    int cmp;
  
  if ( match_length < F - 1 ) {
    cmp = 1;
    key = &text_buf[r + 1];
    p = key[0] + N + 1;
    tmp = 0;
    while ( 1 ) {
      if ( cmp < 0 ) {
        if ( lson[p] == N ) break;
        p = lson[p];
      } else {
        if ( rson[p] == N ) break;
        p = rson[p];
      }
      for ( i = 1; ; ++i ) {
        if ( i < F ) {
          cmp = key[i] - text_buf[p + i];
          if ( key[i] == text_buf[p + i] ) continue;
        }
        break;
      }
      if ( i > tmp ) {
        tmp = i;
        if ( i > F - 1 ) break;
      }
    }
  }
  if ( tmp > match_length ) match_length = 0;
}

void InsertNode(int r) {
  unsigned char *key;
  unsigned tmp, p, i; 
  int cmp = 1;

  key = &text_buf[r];
  p = text_buf[r] + N + 1;
  lson[r] = rson[r] = N;

  match_length = 0;
  while ( 1 ) {
    if ( cmp < 0 ) {
      if ( lson[p] == N ) {
        lson[p] = r;
        dad[r] = p;
        return lazy_match(r);
      }
      p = lson[p];
    } else {
      if ( rson[p] == N ) {
        rson[p] = r;
        dad[r] = p;
        return lazy_match(r);
      }
      p = rson[p];
    }
    for ( i = 1; ; ++i ) {
      if ( i < F ) {
        cmp = key[i] - text_buf[p + i];
        if ( key[i] == text_buf[p + i] ) continue;
      }
      break;
    }
    if ( i >= match_length ) {
      if ( r < p )
        tmp = r - p + N;
      else
        tmp = r - p;
    }
    if ( i >= match_length ) {
      if ( i == match_length ) {
        if ( tmp < match_position )
           match_position = tmp;
      } else 
        match_position = tmp;
      match_length = i;
      if ( i > F - 1 ) break;
    }
  }
  dad[r] = dad[p];
  lson[r] = lson[p];
  rson[r] = rson[p];
  dad[lson[p]] = dad[rson[p]] = r;
  if ( rson[dad[p]] == p )
    rson[dad[p]] = r;
  else
    lson[dad[p]] = r;
  dad[p] = N;
}

void DeleteNode(int p) {
	int q;
	if (dad[p] == NIL) return; 
	if (rson[p] == NIL) q = lson[p];
	else if (lson[p] == NIL) q = rson[p];
	else {
		q = lson[p];
		if (rson[q] != NIL) {
			do {  q = rson[q];  } while (rson[q] != NIL);
			rson[dad[q]] = lson[q];  dad[lson[q]] = dad[q];
			lson[q] = lson[p];  dad[lson[p]] = q;
		}
		rson[q] = rson[p];  dad[rson[p]] = q;
	}
	dad[q] = dad[p];
	if (rson[dad[p]] == p) rson[dad[p]] = q;  else lson[dad[p]] = q;
	dad[p] = NIL;
}

void lzss(FILE* infile, FILE* outfile) {
    int charno = 0, posno = 0;
	int c, i, len, r, s, last_match_length, code_buf_ptr;
	unsigned char code_buf[32], mask;

	InitTree(); 
	code_buf[0] = 0; 
	code_buf_ptr = mask = 1;
	s = 0;  r = N - F;

	for (len = 0; len < F && (c = getc(infile)) != EOF; len++)
		text_buf[r + len] = c;  
	if ((textsize = len) == 0) return; 

	InsertNode(r);
	do {
		if (match_length > len) match_length = len; 
        if (match_length <= THRESHOLD) {
			match_length = 1;  
			code_buf[0] |= mask; 
			code_buf[code_buf_ptr++] = text_buf[r]; 
		} else {
            code_buf[code_buf_ptr++] = match_length - THRESHOLD - 1;
            code_buf[code_buf_ptr++] = (match_position >> 8) & 0xff;
            code_buf[code_buf_ptr++] = match_position;        
            }
		if ((mask <<= 1) == 0) { 
			for (i = 0; i < code_buf_ptr; i++)
				putc(code_buf[i], outfile); 
			codesize += code_buf_ptr;
			code_buf[0] = 0;  
            code_buf_ptr = mask = 1;
		}
		last_match_length = match_length;
		for (i = 0; i < last_match_length && (c = getc(infile)) != EOF; i++) {
			DeleteNode(s);
			text_buf[s] = c;
			if (s < F - 1) text_buf[s + N] = c; 
			s = (s + 1) & (N - 1);  
            r = (r + 1) & (N - 1);
			InsertNode(r);
		}
		textsize += i;
		while (i++ < last_match_length) {	
			DeleteNode(s);					
			s = (s + 1) & (N - 1);  
            r = (r + 1) & (N - 1);
			if (--len) InsertNode(r);		
		}
	} while (len > 0);	
	if (code_buf_ptr > 1) {
		for (i = 0; i < code_buf_ptr; i++) 
            putc(code_buf[i], outfile);
		codesize += code_buf_ptr;
	}
	printf("LZSS Out(%ld)/In(%ld): %.3f\n", codesize, textsize, (double)codesize / textsize);
}

void unlzss(FILE *in, FILE *out) {
    int c, i, j, k, m, r = 0, flags = 0;
    while (1) {
        if (((flags >>= 1) & 256) == 0) {
            if ((c = getc(in)) == EOF) break;
            flags = c | 0xff00;
        }
        if (flags & 1) {
            if ((c = getc(in)) == EOF) break;
            putc(text_buf[r++] = c, out);  
            r &= (N - 1);
        } else {
            if ((j = getc(in)) == EOF) break; // match length
            if ((i = getc(in)) == EOF) break; // byte1 of match position
            if ((m = getc(in)) == EOF) break; // byte0 of match position
            i = (i << 8) | m;
            for (k = 0; k <= j + THRESHOLD; k++) {
                putc(text_buf[r++] = text_buf[(r - 1 - i) & (N - 1)], out);
                r &= (N - 1);
            }
        }
    }
}

uint8_t char_len_table[2304] = { // 288 records
	0x02, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 
	0x0A, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 
	0x29, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 
	0x2B, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 
	0x2D, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x2E, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 
	0x2F, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x8A, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0x8B, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x8C, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0x8D, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x8E, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0x30, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x8F, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0x90, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0x91, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x81, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0x82, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x83, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0x31, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x92, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0x93, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0x0B, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x84, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0x85, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x86, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0x0C, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 
	0x33, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x95, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0x96, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x97, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0x87, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x88, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0x34, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x98, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0x99, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x89, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0x9A, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x9B, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0x8A, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x8B, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0x35, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x9C, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0x9D, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x8C, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0x8D, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x8E, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0x8F, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x90, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0x9E, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x91, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0x92, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x93, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0x94, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x95, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0x96, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x97, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0x36, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x9F, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0x37, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 
	0x98, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x99, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xA0, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x9A, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0x39, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x3A, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 
	0x3B, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0xA1, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0xA2, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xA3, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0x9B, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x9C, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0x9D, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x9E, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0x9F, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xA0, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xA1, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xB0, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0xB1, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xB2, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0xA2, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xA3, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xA4, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xB3, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0xA5, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xB4, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0xB5, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xB6, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0x3C, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0xA4, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0xA5, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xA6, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0xA7, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xA8, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0xA6, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xB7, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0x0D, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0xA9, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0xA7, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xA8, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xA9, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xAA, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xAB, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xAC, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0x3D, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0xAD, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xAA, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xAE, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xAF, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xB0, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xB8, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xB9, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0x3E, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0xB1, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xB2, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xB3, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xBA, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xBB, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0xBC, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xBD, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0x3F, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0xAB, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0xB4, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xB5, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xB6, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xBE, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0xBF, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xC0, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0xAC, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xAD, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0xB7, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xB8, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xB9, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xC1, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0xC2, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xC3, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0xAE, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xAF, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0xB0, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xC4, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0xC5, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xC6, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0xC7, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xC8, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0x40, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0xB1, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0xBA, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xBB, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xC9, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xCA, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0xCB, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xCC, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0xB2, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xB3, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0xCD, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xCE, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0xCF, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xD0, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0xD1, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xDE, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 
	0xB4, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xBC, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xBD, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xBE, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xD2, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xD3, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0xDF, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0xE0, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 
	0xB5, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xD4, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0xD5, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xE1, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 
	0xD6, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xB6, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0xD7, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xE2, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 
	0xD8, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xE3, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 
	0xE4, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0xE5, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 
	0xD9, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xB7, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0xE6, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0xE7, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 
	0xB8, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xBF, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xDA, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xDB, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0xDC, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xE8, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 
	0xE9, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0xEA, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 
	0xC0, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xC1, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xDD, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xEB, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 
	0xDE, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xEC, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 
	0xED, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0xEE, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 
	0x0E, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 
	0xC2, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xC3, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xDF, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xC4, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xEF, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0xF0, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 
	0xC5, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xC6, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xE0, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xE1, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0xC7, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xE2, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0xF1, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0xF2, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 
	0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0xC8, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xC9, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xE3, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0xE4, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xCA, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xCB, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x41, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 
	0xCC, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xCD, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xCE, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xE5, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0xE6, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xE7, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0xE8, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xE9, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0x42, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0xB9, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0xF3, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0xF4, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 
	0xEA, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xF5, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 
	0xCF, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xD0, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xBA, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xD1, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xD2, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xD3, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xD4, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xD5, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xBB, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x43, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 
	0x11, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 
	0x13, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 
	0xBC, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xBD, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	0xBE, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xD6, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 
	0xD7, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xEB, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0xEC, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xED, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 
	0xEE, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xF6, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 
	0xF7, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0xF8, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 
	0xF9, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0xFA, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 
	0xFB, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0xF8, 0x0F, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 
	0xF9, 0x0F, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0xFA, 0x0F, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 
	0xFB, 0x0F, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0xFC, 0x1F, 0x00, 0x00, 0x0D, 0x00, 0x00, 0x00, 
	0xFC, 0x0F, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0xFD, 0x1F, 0x00, 0x00, 0x0D, 0x00, 0x00, 0x00, 
	0xFD, 0x0F, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0xFE, 0x1F, 0x00, 0x00, 0x0D, 0x00, 0x00, 0x00, 
	0xFF, 0x1F, 0x00, 0x00, 0x0D, 0x00, 0x00, 0x00, 0xBF, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00
};

uint8_t pos_table[256] = { // 32 records
	0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 
	0x06, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 
	0x0F, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 
	0x11, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 
	0x13, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 
	0x2A, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x2B, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 
	0x2C, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x2D, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 
	0x2E, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x2F, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 
	0x30, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x31, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 
	0x32, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x33, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 
	0x34, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x35, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 
	0x36, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x37, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 
	0x38, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x39, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 
	0x3A, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x3B, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 
	0x3C, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x3D, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 
	0x3E, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00
};

void huff(FILE* in, FILE* out) {
    uint32_t preno = 0, precode = 0;
    void putChar(uint32_t code, uint32_t no) {
        uint32_t tmpno, tmpcode;
        codesize += no;
        if (preno + no > 7){
            do {
                no -= tmpno = 8 - preno;
                tmpcode = code >> no;
                fputc(tmpcode | (precode << tmpno), out);
                code -= tmpcode << no;
                preno = precode = 0;
            } while (no > 7);
            preno = no;
            precode = code;	
        } else {
            preno += no;
            precode = code | (precode << no);
        }
    }
    textsize = codesize; codesize = 0;
    int c, i, j, k, m, flags = 0;
    while ( 1 ) {
        if (((flags >>= 1) & 256) == 0) {
            if ((c = getc(in)) == EOF) break;
            flags = c | 0xFF00;
        }
        if (flags & 1) {
            if ((c = getc(in)) == EOF) break;
            putChar(*(uint32_t*)&char_len_table[8 * (unsigned char)c], *(uint32_t*)&char_len_table[8 * (unsigned char)c + 4]); // lookup in char table
        } else {
            if ((j = getc(in)) == EOF) break; // match length
            if ((i = getc(in)) == EOF) break; // byte1 of match position
            if ((m = getc(in)) == EOF) break; // byte0 of match position
            putChar(*(uint32_t*)&char_len_table[8 * (j + 256)], *(uint32_t*)&char_len_table[8 * (j + 256) + 4]); // lookup in len table
            i = m | (i << 8);            
            putChar(*(uint32_t*)&pos_table[8 * (int)(i >> 7)], *(uint32_t*)&pos_table[8 * (int)(i >> 7) + 4]); // lookup in pos table
            putChar(i - (i >> 7 << 7), 7);
        }
    }
    putc(precode << (8 - preno), out);
    codesize += preno;
    codesize = (unsigned int)codesize >> 3;
    printf("LZHS Out(%d)/In(%d): %.4f\n", codesize, textsize, (double)codesize / textsize);
}

struct header_t {
    uint32_t uncompressedSize, compressedSize;
	uint8_t checksum, spare[7];
} header;

int DecodeChar(void) {
}

int DecodePosition(void) {
}

void unhuff(FILE* in, FILE* out) {
    int r = N - F, count, c, i, j, k;
    for (count = 0; count < header.compressedSize; ) {
        c = DecodeChar();
        if (c < 256) {
            if (putc(c, out) == EOF) break;
            text_buf[r++] = (unsigned char)c;
            r &= (N - 1);
            count++;
        } else {
            i = (r - DecodePosition() - 1) & (N - 1);
            j = c - 255 + THRESHOLD;
            for (k = 0; k < j; k++) {
                c = text_buf[(i + k) & (N - 1)];
                if (putc(c, out) == EOF) break;
                text_buf[r++] = (unsigned char)c;
                r &= (N - 1);
                count++;
            }
        }
    }
}

#include <fcntl.h>

void test(void) {
	FILE* in = fopen("conv", "rb");
	FILE* out = fopen("tmp2.lzs", "wb");
	lzss(in, out);
	fclose(in);	
	fclose(out);	

	in = fopen("tmp.lzs", "rb");
	out = fopen("conv2", "r+b");
	unlzss(in, out);
	fclose(in);	
	int fileSize = ftell(out);

    unsigned char* buffer = (unsigned char*) malloc(sizeof(char) * fileSize);
	rewind(out);
	fread(buffer, 1, fileSize, out);
	fclose(out);
	
	ARMThumb_Convert(buffer, fileSize, 0, 0);
	out = fopen("u-boot.tmp", "wb");
	fwrite(buffer, 1, fileSize, out);
	fclose(out);
	
	header.checksum = 0; int i;
	for (i = 0; i < fileSize; ++i) header.checksum += buffer[i];
	printf("Unlzss file size: %d bytes, checksum: %02X\n", fileSize, header.checksum);
    free(buffer);

	in = fopen("u-boot.lzhs", "rb");
	out = fopen("tmp2.lzs", "r+b");
	fread(&header, 1, sizeof(header), in);
	printf("Uncompressed size: %d, compressed size: %d, checksum: %02X\n", header.uncompressedSize, header.compressedSize, header.checksum);
    unhuff(in, out);
	fclose(in);	
	fclose(out);	
    
	in = fopen("tmp2.lzs", "rb");
	out = fopen("tmp2.lzhs", "wb");
    fwrite(&header, 1, sizeof(header), out);
	huff(in, out);
	fclose(in);	
	fclose(out);	
    
	exit(0);
}

int main(int argc, char *argv[]) {
	//test();
    printf("\nLG Electronics digital TV firmware package (EPK) extractor 3.9 by sirius (http://openlgtv.org.ru)\n\n");
	if (argc < 2) {
		printf("Thanks to xeros, tbage, jenya, Arno1, rtokarev, cronix, lprot, Smx and all other guys from openlgtv project for their kind assistance.\n\n");
		printf("Usage: epk2extract [-options] FILENAME\n\n");
		printf("Options:\n");
		printf("  -c : extract to current directory instead of source file directory\n");
		#ifdef __CYGWIN__
			puts("\nPress any key to continue...");
			getch();
		#endif
		exit(1);
	}

	current_dir = malloc(PATH_MAX);
	getcwd(current_dir, PATH_MAX);
	printf("Current directory: %s\n", current_dir);
	readlink("/proc/self/exe", exe_dir, 1024);
	config_opts.config_dir = dirname(exe_dir);
	config_opts.dest_dir = NULL;

	int opt;
	while ((opt = getopt(argc, argv, "c")) != -1) {
		switch (opt) {
		case 'c': {
			config_opts.dest_dir = current_dir;
			break;
		}
		case ':': {
			printf("Option `%c' needs a value\n\n", optopt);
			exit(1);
			break;
		}
		case '?': {
			printf("Unknown option: `%c'\n\n", optopt);
			exit(1);
		}
		}
	}

	#ifdef __CYGWIN__
		char posix[PATH_MAX];
		cygwin_conv_path(CCP_WIN_A_TO_POSIX, argv[optind], posix, PATH_MAX);
		char *input_file = posix;
	#else
		char *input_file = argv[optind];
	#endif
	printf("Input file: %s\n", input_file);
	if (config_opts.dest_dir == NULL) config_opts.dest_dir = dirname(strdup(input_file));
	printf("Destination directory: %s\n", config_opts.dest_dir);
	int exit_code = handle_file(input_file, &config_opts);
	if(exit_code == EXIT_FAILURE) {
		printf("Unsupported input file format: %s\n\n", input_file);
		#ifdef __CYGWIN__
			puts("Press any key to continue...");
			getch();
		#endif
		return exit_code;
	}
	printf("\nExtraction is finished.\n\n");
	#ifdef __CYGWIN__
		puts("Press any key to continue...");
		getch();
	#endif
	return exit_code;
}