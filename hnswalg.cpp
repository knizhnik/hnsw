#include "hnswalg.h"


#ifdef USE_PREFETCH
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

#define PREFETCH(addr,hint) _mm_prefetch(addr, hint)
#else
#define PREFETCH(addr,hint)
#endif

HierarchicalNSW::HierarchicalNSW(size_t dim_, size_t maxelements_, size_t M_, size_t maxM_, size_t efConstruction_)
{
    dim = dim_;
    data_size = dim * sizeof(coord_t);

    efConstruction = efConstruction_;

    maxelements = maxelements_;
    M = M_;
    maxM = maxM_;
    size_links_level0 = (maxM + 1) * sizeof(idx_t);
    size_data_per_element = size_links_level0 + data_size  + sizeof(label_t);
    offset_data = size_links_level0;
	offset_label = offset_data + data_size;

    enterpoint_node = 0;
    cur_element_count = 0;
	dist_calc = 0;
}

std::priority_queue<std::pair<dist_t, idx_t>> HierarchicalNSW::searchBaseLayer(const coord_t *point, size_t ef)
{
	std::vector<uint32_t> visited;
	visited.resize((cur_element_count + 31) >> 5);

    std::priority_queue<std::pair<dist_t, idx_t >> topResults;
    std::priority_queue<std::pair<dist_t, idx_t >> candidateSet;

    dist_t dist = fstdistfunc(point, getDataByInternalId(enterpoint_node));

    topResults.emplace(dist, enterpoint_node);
    candidateSet.emplace(-dist, enterpoint_node);
    visited[enterpoint_node >> 5] = 1 << (enterpoint_node & 31);
    dist_t lowerBound = dist;

    while (!candidateSet.empty())
    {
        std::pair<dist_t, idx_t> curr_el_pair = candidateSet.top();
        if (-curr_el_pair.first > lowerBound)
            break;

        candidateSet.pop();
        idx_t curNodeNum = curr_el_pair.second;

        idx_t* data = get_linklist0(curNodeNum);
        size_t size = *data++;

        PREFETCH(getDataByInternalId(*data), _MM_HINT_T0);

        for (size_t j = 0; j < size; ++j) {
            size_t tnum = *(data + j);

            PREFETCH(getDataByInternalId(*(data + j + 1)), _MM_HINT_T0);

            if (!(visited[tnum >> 5] & (1 << (tnum & 31)))) {
				visited[tnum >> 5] |= 1 << (tnum & 31);

                dist = fstdistfunc(point, getDataByInternalId(tnum));

                if (topResults.top().first > dist || topResults.size() < ef) {
                    candidateSet.emplace(-dist, tnum);

                    PREFETCH(get_linklist0(candidateSet.top().second), _MM_HINT_T0);
                    topResults.emplace(dist, tnum);

                    if (topResults.size() > ef)
                        topResults.pop();

                    lowerBound = topResults.top().first;
                }
            }
        }
    }
    return topResults;
}


void HierarchicalNSW::getNeighborsByHeuristic(std::priority_queue<std::pair<dist_t, idx_t>> &topResults, size_t NN)
{
    if (topResults.size() < NN)
        return;

    std::priority_queue<std::pair<dist_t, idx_t>> resultSet;
    std::vector<std::pair<dist_t, idx_t>> returnlist;

    while (topResults.size() > 0) {
        resultSet.emplace(-topResults.top().first, topResults.top().second);
        topResults.pop();
    }

    while (resultSet.size()) {
        if (returnlist.size() >= NN)
            break;
        std::pair<dist_t, idx_t> curen = resultSet.top();
        dist_t dist_to_query = -curen.first;
        resultSet.pop();
        bool good = true;
        for (std::pair<dist_t, idx_t> curen2 : returnlist) {
            dist_t curdist = fstdistfunc(getDataByInternalId(curen2.second),
                                         getDataByInternalId(curen.second));
            if (curdist < dist_to_query) {
                good = false;
                break;
            }
        }
        if (good) returnlist.push_back(curen);
    }
    for (std::pair<dist_t, idx_t> elem : returnlist)
        topResults.emplace(-elem.first, elem.second);
}

void HierarchicalNSW::mutuallyConnectNewElement(const coord_t *point, idx_t cur_c,
                               std::priority_queue<std::pair<dist_t, idx_t>> topResults)
{
    getNeighborsByHeuristic(topResults, M);

    std::vector<idx_t> res;
    res.reserve(M);
    while (topResults.size() > 0) {
        res.push_back(topResults.top().second);
        topResults.pop();
    }
    {
        idx_t* data = get_linklist0(cur_c);
        if (*data)
            throw std::runtime_error("Should be blank");

        *data++ = res.size();

        for (size_t idx = 0; idx < res.size(); idx++) {
            if (data[idx])
                throw std::runtime_error("Should be blank");
            data[idx] = res[idx];
        }
    }
    for (size_t idx = 0; idx < res.size(); idx++) {
        if (res[idx] == cur_c)
            throw std::runtime_error("Connection to the same element");

        size_t resMmax = maxM;
        idx_t *ll_other = get_linklist0(res[idx]);
        idx_t sz_link_list_other = *ll_other;

        if (sz_link_list_other > resMmax || sz_link_list_other < 0)
            throw std::runtime_error("Bad sz_link_list_other");

        if (sz_link_list_other < resMmax) {
            idx_t *data = ll_other + 1;
            data[sz_link_list_other] = cur_c;
            *ll_other = sz_link_list_other + 1;
        } else {
            // finding the "weakest" element to replace it with the new one
            idx_t *data = ll_other + 1;
            dist_t d_max = fstdistfunc(getDataByInternalId(cur_c), getDataByInternalId(res[idx]));
            // Heuristic:
            std::priority_queue<std::pair<dist_t, idx_t>> candidates;
            candidates.emplace(d_max, cur_c);

            for (size_t j = 0; j < sz_link_list_other; j++)
                candidates.emplace(fstdistfunc(getDataByInternalId(data[j]), getDataByInternalId(res[idx])), data[j]);

            getNeighborsByHeuristic(candidates, resMmax);

            size_t indx = 0;
            while (!candidates.empty()) {
                data[indx] = candidates.top().second;
                candidates.pop();
                indx++;
            }
            *ll_other = indx;
        }
    }
}

void HierarchicalNSW::addPoint(const coord_t *point, label_t label)
{
    if (cur_element_count >= maxelements) {
        throw std::runtime_error("The number of elements exceeds the specified limit");
    }
    idx_t cur_c = cur_element_count++;
    memset((char *) get_linklist0(cur_c), 0, size_data_per_element);
    memcpy(getDataByInternalId(cur_c), point, data_size);
    memcpy(getExternalLabel(cur_c), &label, sizeof label);

    // Do nothing for the first element
    if (cur_c != 0) {
        std::priority_queue <std::pair<dist_t, idx_t>> topResults = searchBaseLayer(point, efConstruction);
        mutuallyConnectNewElement(point, cur_c, topResults);
    }
};

std::priority_queue<std::pair<dist_t, label_t>> HierarchicalNSW::searchKnn(const coord_t *query, size_t k)
{
	std::priority_queue<std::pair<dist_t, label_t>> topResults;
	auto topCandidates = searchBaseLayer(query, k);
    while (topCandidates.size() > k) {
        topCandidates.pop();
	}
	while (!topCandidates.empty()) {
		std::pair<dist_t, idx_t> rez = topCandidates.top();
		label_t label;
		memcpy(&label, getExternalLabel(rez.second), sizeof(label));
		topResults.push(std::pair<dist_t, label_t>(rez.first, label));
		topCandidates.pop();
	}

    return topResults;
};

dist_t HierarchicalNSW::fstdistfunc(const coord_t *x, const coord_t *y)
{
	dist_t 	distance = 0.0;
	size_t  n = dim;

	dist_calc++;

	for (size_t i = 0; i < n; i++)
	{
		dist_t diff = x[i] - y[i];
		distance += diff * diff;
	}
	return distance;
}

bool hnsw_search(HierarchicalNSW* hnsw, const coord_t *point, size_t efSearch, size_t* n_results, label_t** results)
{
	try
	{
		auto result = hnsw->searchKnn(point, efSearch);
		size_t nResults = result.size();
		*results = (label_t*)malloc(nResults*sizeof(label_t));
		for (size_t i = nResults; i-- != 0;)
		{
			(*results)[i] = result.top().second;
			result.pop();
		}
		*n_results = nResults;
		return true;
	}
	catch (std::exception& x)
	{
		return false;
	}
}

bool hnsw_add_point(HierarchicalNSW* hnsw, const coord_t *point, label_t label)
{
	try
	{
		hnsw->addPoint(point, label);
		return true;
	}
	catch (std::exception& x)
	{
		fprintf(stderr, "Catch %s\n", x.what());
		return false;
	}
}

void hnsw_init(HierarchicalNSW* hnsw, size_t dims, size_t maxelements, size_t M, size_t maxM, size_t efConstruction)
{
	new ((void*)hnsw) HierarchicalNSW(dims, maxelements, M, maxM, efConstruction);
}


int hnsw_dimensions(HierarchicalNSW* hnsw)
{
	return (int)hnsw->dim;
}

size_t hnsw_count(HierarchicalNSW* hnsw)
{
	return hnsw->cur_element_count;
}

size_t hnsw_sizeof(void)
{
	return sizeof(HierarchicalNSW);
}
