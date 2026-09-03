/*
 * hashes.h
 *
 *  Created on: Aug 16, 2017
 *      Author: root
 */

#ifndef KZ_HASHES_H_
#define KZ_HASHES_H_

/* adapted from kamailio */

#define kz_ch_h_inc h+=v^(v>>3)

static inline unsigned int kz_core_hash(const char *s1, int len, const unsigned int size)
{
	char *p, *end;
//	int len = strlen(s1);
	register unsigned v;
	register unsigned h;

	h=0;

	end=((char*)s1)+len;
	for ( p=(char*)s1 ; p<=(end-4) ; p+=4 ){
		v=(*p<<24)+(p[1]<<16)+(p[2]<<8)+p[3];
		kz_ch_h_inc;
	}
	v=0;
	for (; p<end ; p++){ v<<=8; v+=*p;}
	kz_ch_h_inc;

	h=((h)+(h>>11))+((h>>13)+(h>>23));
	return size?((h)&(size-1)):h;
}


#endif /* HASHES_H_ */
