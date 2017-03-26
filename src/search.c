#include <err.h>
#include <sysexits.h>
#include <limits.h>

//from giggle
#include <src/giggle_index.h>
#include <src/ll.h>
#include <src/file_read.h>
#include <src/lists.h>
#include <src/util.h>

#include "search.h"

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

//{{{struct stix_breakpoint *stix_region_to_breakpoint(char *region)
struct stix_breakpoint *stix_region_to_breakpoint(char *region)
{
    struct stix_breakpoint *bp = (struct stix_breakpoint *)
        calloc(1, sizeof(struct stix_breakpoint));

    if (parse_region(region,
                     &(bp->chrm),
                     &(bp->start),
                     &(bp->end)) != 0)
        errx(EX_USAGE,
             "Error parsing region '%s'\n",
             region);

    bp->strand = 0;

    return bp;
}
//}}}

//{{{uint32_t stix_parse_result(char *result,
uint32_t stix_parse_result(char *result,
                           struct stix_breakpoint **left,
                           struct stix_breakpoint **right,
                           uint32_t *evidence_type)
{
    if (*left == NULL) {
        *left = (struct stix_breakpoint *)
                malloc(sizeof(struct stix_breakpoint));
        (*left)->chrm = NULL;
    }

    if (*right == NULL) {
        *right = (struct stix_breakpoint *)
                malloc(sizeof(struct stix_breakpoint));
        (*right)->chrm = NULL;
    }

    if ((*left)->chrm != NULL)
        free((*left)->chrm);
    int ret = asprintf(&((*left)->chrm), "%s", strtok(result, "\t"));
    if (ret == -1)
        errx(1, "ERROR tix_parse_result() asprintf error");
    (*left)->start = atoi(strtok(NULL, "\t"));
    (*left)->end = atoi(strtok(NULL, "\t"));
    (*left)->strand = atoi(strtok(NULL, "\t"));

    if ((*right)->chrm != NULL)
        free((*right)->chrm);
    ret = asprintf(&((*right)->chrm), "%s", strtok(NULL, "\t"));
    if (ret == -1)
        errx(1, "ERROR tix_parse_result() asprintf error");
    (*right)->start = atoi(strtok(NULL, "\t"));
    (*right)->end = atoi(strtok(NULL, "\t"));
    (*right)->strand = atoi(strtok(NULL, "\t"));

    *evidence_type = atoi(strtok(NULL, "\t"));

    return 0;
}
//}}}

//{{{uint32_t stix_check_sv(struct stix_breakpoint *q_left_bp,
uint32_t stix_check_sv(struct stix_breakpoint *q_left_bp,
                        struct stix_breakpoint *q_right_bp,
                        struct stix_breakpoint *in_left_bp,
                        struct stix_breakpoint *in_right_bp,
                        uint32_t evidence_type,
                        uint32_t slop,
                        enum stix_sv_type sv_type)
{
   switch (sv_type) {
        case DEL: 
            return stix_check_del(q_left_bp,
                                  q_right_bp,
                                  in_left_bp,
                                  in_right_bp,
                                  evidence_type,
                                  slop);
            break;
        case DUP: 
            errx(1,"DUP not yet supported");
            break;
        case INS:
            errx(1,"INS not yet supported");
            break;
        case INV:
            errx(1,"INV not yet supported");
            break;
        case BND:
            errx(1,"BND not yet supported");
            break;
        default:
            errx(1,"Unknown SV type");
   } 
}
//}}}

//{{{uint32_t stix_check_del(struct stix_breakpoint *q_left_bp,
uint32_t stix_check_del(struct stix_breakpoint *q_left_bp,
                        struct stix_breakpoint *q_right_bp,
                        struct stix_breakpoint *in_left_bp,
                        struct stix_breakpoint *in_right_bp,
                        uint32_t evidence_type,
                        uint32_t slop)
{
    // Check strand config +- for paired-end and ++ or -- for split-read
    if (evidence_type == 0) { // paired-end
        if (in_left_bp->strand == in_right_bp->strand)
            return 0;
    } else { //split read
        if (in_left_bp->strand != in_right_bp->strand)
            return 0;
    }

    // Make sure its intra-chhromosomal
    if (strcmp(q_right_bp->chrm, in_right_bp->chrm) != 0)
        return 0;

    // Make sure the right sides intersect 
    if ( (in_right_bp->end >= q_right_bp->start) &&       // end after start
         (in_right_bp->start < q_right_bp->end + slop) )  // start before end
        return 1;
    else
        return 0;
}
//}}}

//{{{void stix_run_giggle_query(struct giggle_index **gi,
uint32_t stix_run_giggle_query(struct giggle_index **gi,
                               char *giggle_index_dir,
                               enum stix_sv_type sv_type,
                               struct stix_breakpoint *q_left_bp,
                               struct stix_breakpoint *q_right_bp,
                               uint32_t slop,
                               uint32_t *sample_ids,
                               uint32_t num_samples,
                               struct uint_pair **sample_alt_depths)
{
    if (*gi == NULL) {
        *gi = giggle_load(giggle_index_dir,
                          uint64_t_ll_giggle_set_data_handler);
        if (*gi == NULL)
            errx(1,
                 "ERROR stix_run_giggle_query(): "
                 "Error loading giggle index %s.", 
                 giggle_index_dir);

        giggle_data_handler.giggle_collect_intersection =
                giggle_collect_intersection_data_in_block;
        giggle_data_handler.map_intersection_to_offset_list =
                leaf_data_map_intersection_to_offset_list;
    }


    struct giggle_query_result *gqr = giggle_query(*gi,
                                                   q_left_bp->chrm,
                                                   q_left_bp->start - slop,
                                                   q_left_bp->end,
                                                   NULL);

    uint32_t i, N = gqr->num_files;
    if ((sample_ids != NULL) & (num_samples > 0))
        N = num_samples;

    if (*sample_alt_depths == NULL)
        *sample_alt_depths =
                (struct uint_pair *)malloc(N * sizeof(struct uint_pair));

    memset(*sample_alt_depths, 0, N * sizeof(struct uint_pair));

    struct stix_breakpoint *in_left_bp = NULL, *in_right_bp = NULL;
    uint32_t evidence_type, idx;
    for(i = 0; i < N; i++) {
        if (num_samples > 0)
            idx = sample_ids[i];
        else
            idx = i;


        char *result;
        struct giggle_query_iter *gqi = giggle_get_query_itr(gqr, idx);

        while (giggle_query_next(gqi, &result) == 0) {

            uint32_t ret = stix_parse_result(result,
                                             &in_left_bp,
                                             &in_right_bp,
                                             &evidence_type);

            uint32_t hit = stix_check_sv(q_left_bp,
                                         q_right_bp,
                                         in_left_bp,
                                         in_right_bp,
                                         evidence_type,
                                         slop,
                                         sv_type);
            if (hit == 1) {
                if (evidence_type == 0)
                    (*sample_alt_depths)[i].first += 1; //pairend-read
                else
                    (*sample_alt_depths)[i].second += 1; //split-reads
            }
        }
        giggle_iter_destroy(&gqi);

        /*
        struct file_data *fd = 
            (struct file_data *)unordered_list_get((*gi)->file_idx->index, i);
        fprintf(stderr,
                "%s %u %u\n",
                fd->file_name,
                (*sample_alt_depths)[i].first,
                (*sample_alt_depths)[i].second);
        */
    }

    free(in_left_bp->chrm);
    free(in_right_bp->chrm);
    free(in_left_bp);
    free(in_right_bp);

    giggle_query_result_destroy(&gqr);

    return N;
}
//}}}

//{{{uint32_t stix_get_uniq(uint32_t *full,
uint32_t stix_get_uniq(uint32_t *full,
                       uint32_t num_full,
                       uint32_t **uniq)
{
    qsort(full, num_full, sizeof(uint32_t), uint32_t_cmp);

    *uniq = (uint32_t *) malloc(num_full*sizeof(uint32_t));

    uint32_t i, u_i = 0;

    for (i = 0; i < num_full; ++i) {
        if ( ((i + 1) == num_full) || (full[i] != full[i+1])) {
            (*uniq)[u_i++] =  full[i];
        }
    }

    *uniq = realloc(*uniq, u_i * sizeof(uint32_t));

    return u_i;
}
//}}}

//{{{uint32_t stix_get_quartile_counts(uint32_t *full,
uint32_t stix_get_quartile_counts(uint32_t *full,
                                  uint32_t num_full,
                                  uint32_t *Q1, 
                                  uint32_t *Q2, 
                                  uint32_t *Q3, 
                                  int32_t *counts)
{
    memset(counts, 0, 4*sizeof(int32_t));


    uint32_t *uniq;
    uint32_t num_uniq = stix_get_uniq(full, num_full, &uniq);

    if (num_uniq >= 3) {
        *Q1 = uniq[num_uniq/4];
        *Q2 = uniq[num_uniq/2];
        *Q3 = uniq[MIN(num_uniq/2 + 1 + num_uniq/4, num_uniq-1)];

        counts[0] = stix_bsearch_seq(*Q1, full, num_full, -1, num_full);

        counts[1] = stix_bsearch_seq(*Q2, full, num_full, -1, num_full) -
                counts[0];

        counts[2] = stix_bsearch_seq(*Q3, full, num_full, -1, num_full) -
                counts[1] - counts[0];

        counts[3] = num_full - counts[2] - counts[1] - counts[0];
    } else if (num_uniq == 3) {
        *Q1 = uniq[0];
        *Q2 = uniq[1];
        *Q3 = uniq[2];
        counts[0] = 0;
        counts[1] = 1;
        counts[2] = 2;
        counts[3] = 3;
    } else if (num_uniq == 2) {
        *Q1 = 0;
        *Q2 = uniq[0];
        *Q3 = uniq[1];
        counts[0] = 0;
        counts[1] = 0;
        counts[2] = 1;
        counts[3] = 1;
    } else if (num_uniq == 1) {
        *Q1 = 0;
        *Q2 = 0;
        *Q3 = uniq[0];
        counts[0] = 0;
        counts[1] = 0;
        counts[2] = 0;
        counts[3] = 1;
    }

    free(uniq);
    return 0;
}
//}}}

//{{{ int32_t stix_bsearch_seq(uint32_t key,
int32_t stix_bsearch_seq(uint32_t key,
                         uint32_t *D,
                         uint32_t D_size,
                         int32_t lo,
                         int32_t hi)
{
        int32_t i = 0;
        uint32_t mid;
        while ( hi - lo > 1) {
                ++i;
                mid = (hi + lo) / 2;
                if ( D[mid] < key )
                        lo = mid;
                else
                        hi = mid;
        }

        return hi;
}
//}}}

//{{{ uint32_t stix_get_summary(struct uint_pair *sample_alt_depths,
uint32_t stix_get_summary(struct uint_pair *sample_alt_depths,
                          uint32_t *sample_ids,
                          uint32_t num_samples,
                          int32_t *zero_count,
                          int32_t *one_count,
                          uint32_t *Q1,
                          uint32_t *Q2,
                          uint32_t *Q3,
                          uint32_t *min,
                          uint32_t *max,
                          int32_t *counts)
{
    *zero_count = 0;
    *one_count = 0;
    *min = INT_MAX;
    *max = 0;

    uint32_t *full = (uint32_t *)malloc(num_samples * sizeof(uint32_t));
    uint32_t full_i = 0;

    uint32_t i, sum, idx;
    for (i = 0; i < num_samples;  ++i) {
        idx = i;

        sum = sample_alt_depths[idx].first + sample_alt_depths[idx].second;
        if (sum == 0)
            *zero_count = *zero_count + 1;
        else if (sum == 1)
            *one_count = *one_count + 1;
        else {
            full[full_i] = sum;
            full_i += 1;
        }

        if (*min > sum)
            *min = sum;
        
        if (*max < sum)
            *max = sum;
    }


    uint32_t ret = stix_get_quartile_counts(full,
                                            full_i,
                                            Q1, 
                                            Q2, 
                                            Q3, 
                                            counts);
    free(full);
    return 0;
}
//}}}