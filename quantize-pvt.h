#ifndef LOOP_PVT_H
#define LOOP_PVT_H

#define IXMAX_VAL 8206 /* ix always <= 8191+15.    see count_bits() */
#define PRECALC_SIZE (IXMAX_VAL+2)

extern FLOAT masking_lower;
extern int convert_mdct, convert_psy, reduce_sidechannel;
extern unsigned nr_of_sfb_block[6][3][4];
extern int pretab[21];

struct scalefac_struct
{
   int l[1+SBMAX_l];
   int s[1+SBMAX_s];
};

extern struct scalefac_struct scalefac_band;
extern struct scalefac_struct sfBandIndex[6];

extern FLOAT8 pow43[PRECALC_SIZE];

#define Q_MAX 256

extern FLOAT8 pow20[Q_MAX];
extern FLOAT8 ipow20[Q_MAX];

#if RH_ATH
extern FLOAT8 ATH_mdct_long[576], ATH_mdct_short[192];
#endif

FLOAT8 ATHformula(FLOAT8 f);
void compute_ath(layer *info,FLOAT8 ATH_l[SBPSY_l],FLOAT8 ATH_s[SBPSY_l]);
void ms_convert(FLOAT8 xr[2][576],FLOAT8 xr_org[2][576]);
void on_pe(FLOAT8 pe[2][2],III_side_info_t *l3_side,
int targ_bits[2],int mean_bits, int gr);
void reduce_side(int targ_bits[2],FLOAT8 ms_ener_ratio,int mean_bits);


void outer_loop( FLOAT8 xr[576],     /*vector of the magnitudees of the spectral values */
                int bits,
		FLOAT8 noise[4],
                FLOAT8 targ_noise[4],    /* VBR target noise info */
                III_psy_xmin *l3_xmin, /* the allowed distortion of the scalefactor */
                int l3_enc[576],    /* vector of quantized values ix(0..575) */
		frame_params *fr_ps,
		 III_scalefac_t *scalefac, /* scalefactors */
		 gr_info *,
		III_side_info_t *l3_side,
		III_psy_ratio *ratio, 
		FLOAT8 ms_ratio,
		int gr, int ch);


void outer_loop_dual( FLOAT8 xr[2][576],     /*vector of the magnitudees of the spectral values */
		 FLOAT8 xr_org[2][576],
                int mean_bits,
                int bit_rate,
		int best_over[2],
                III_psy_xmin l3_xmin[2], /* the allowed distortion of the scalefactor */
                int l3_enc[2][576],    /* vector of quantized values ix(0..575) */
		frame_params *fr_ps,
                III_scalefac_t scalefac[2], /* scalefactors */
                int gr,
		III_side_info_t *l3_side,
		III_psy_ratio ratio[2], 
		FLOAT8 pe[2][2],
		FLOAT8 ms_ratio[2]);




void iteration_init( III_side_info_t *l3_side, int l3_enc[2][2][576],
		frame_params *fr_ps);

int inner_loop( FLOAT8 xrpow[576],
                int l3_enc[576],
                int max_bits,
                gr_info *cod_info);

int calc_xmin( FLOAT8 xr[576],
               III_psy_ratio *ratio,
               gr_info *cod_info,
               III_psy_xmin *l3_xmin);


int scale_bitcount( III_scalefac_t *scalefac, gr_info *cod_info);
int scale_bitcount_lsf( III_scalefac_t *scalefac, gr_info *cod_info);
int calc_noise1( FLOAT8 xr[576],
                 int ix[576],
                 gr_info *cod_info,
                 FLOAT8 xfsf[4][SBPSY_l], 
		 FLOAT8 distort[4][SBPSY_l],
                 III_psy_xmin *l3_xmin,
		 III_scalefac_t *,
                 FLOAT8 *noise, FLOAT8 *tot_noise, FLOAT8 *max_noise);

void calc_noise2( FLOAT8 xr[2][576],
                 int ix[2][576],
                 gr_info *cod_info[2],
                 FLOAT8 xfsf[2][4][SBPSY_l], 
		 FLOAT8 distort[2][4][SBPSY_l],
                 III_psy_xmin l3_xmin[2],
		 III_scalefac_t scalefac[2],
		 int over[2], 
                 FLOAT8 noise[2], FLOAT8 tot_noise[2], FLOAT8 max_noise[2]);


int loop_break( III_scalefac_t *scalefac, gr_info *cod_info);

void amp_scalefac_bands(FLOAT8 xrpow[576],
			gr_info *cod_info,
			III_scalefac_t *scalefac,
			FLOAT8 distort[4][SBPSY_l]);

void quantize_xrpow( FLOAT8 xr[576],
               int  ix[576],
               gr_info *cod_info );
void quantize_xrpow_ISO( FLOAT8 xr[576],
               int  ix[576],
               gr_info *cod_info );

int
new_choose_table( int ix[576],
		  unsigned int begin,
		  unsigned int end, int * s );

int bin_search_StepSize2(int desired_rate, FLOAT8 start, int ix[576],
           FLOAT8 xrs[576], FLOAT8 xrspow[576], gr_info * cod_info);
int count_bits(int  *ix, FLOAT8 xr[576], gr_info *cod_info);


int quant_compare(
int best_over,FLOAT8 best_tot_noise,FLOAT8 best_over_noise,FLOAT8 best_max_over,
int over,FLOAT8 tot_noise, FLOAT8 over_noise,FLOAT8 max_noise);

int VBR_compare(
int best_over,FLOAT8 best_tot_noise,FLOAT8 best_over_noise,FLOAT8 best_max_over,
int over,FLOAT8 tot_noise, FLOAT8 over_noise,FLOAT8 max_noise);

void best_huffman_divide(int gr, int ch, gr_info *cod_info, int *ix);

void best_scalefac_store(int gr, int ch,
			 III_side_info_t *l3_side,
			 III_scalefac_t scalefac[2][2]);

void init_outer_loop(
    FLOAT8 xr[576],        /*  could be L/R OR MID/SIDE */
    III_scalefac_t *scalefac, /* scalefactors */
    gr_info *cod_info,
    III_side_info_t *l3_side);

#endif
