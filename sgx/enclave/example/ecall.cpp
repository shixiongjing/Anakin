#include "anakin_config.h"

#include <algorithm>
#include "stdio.h"
#include "stdlib.h"

#include "graph.h"
#include "net.h"
#include "saber/core/tensor_op.h"
#include "mkl.h"

#include <sgx_tseal.h>
#include <sgx_tcrypto.h>
#include <sgx_trts.h>
//#include <sgx_tprotected_fs.h>

#define BUFLEN (1024U * 1024U * 2U)
#define SGX_AESGCM_MAC_SIZE 16
#define SGX_AESGCM_IV_SIZE 12
//#define USE_ENCRYPTION 1
static sgx_aes_gcm_128bit_key_t key = { 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf };

namespace {

using namespace anakin;

std::unique_ptr<graph::Graph<X86, Precision::FP32>> ModelGraph;
std::unique_ptr<Net<X86, Precision::FP32>> ModelNet;

}

namespace anakin {


extern "C" void decryptMessage(char *encMessageIn, size_t len, char *decMessageOut, size_t lenOut)
{
	uint8_t *encMessage = (uint8_t *) encMessageIn;
	uint8_t p_dst[BUFLEN] = {0};

	sgx_rijndael128GCM_decrypt(
		&key,
		encMessage + SGX_AESGCM_MAC_SIZE + SGX_AESGCM_IV_SIZE,
		lenOut,
		p_dst,
		encMessage + SGX_AESGCM_MAC_SIZE, SGX_AESGCM_IV_SIZE,
		NULL, 0,
		(sgx_aes_gcm_128bit_tag_t *) encMessage);
	memcpy(decMessageOut, p_dst, lenOut);
        //emit_debug((char *) p_dst);
}

extern "C" void encryptMessage(char *decMessageIn, size_t len, char *encMessageOut, size_t lenOut)
{
	uint8_t *origMessage = (uint8_t *) decMessageIn;
	uint8_t p_dst[BUFLEN] = {0};

	// Generate the IV (nonce)
	sgx_read_rand(p_dst + SGX_AESGCM_MAC_SIZE, SGX_AESGCM_IV_SIZE);

	sgx_rijndael128GCM_encrypt(
		&key,
		origMessage, len,
		p_dst + SGX_AESGCM_MAC_SIZE + SGX_AESGCM_IV_SIZE,
		p_dst + SGX_AESGCM_MAC_SIZE, SGX_AESGCM_IV_SIZE,
		NULL, 0,
		(sgx_aes_gcm_128bit_tag_t *) (p_dst));
	memcpy(encMessageOut,p_dst,lenOut);
}



extern "C" int setup_model(const char *model_name) {
    ModelGraph.reset(new graph::Graph<X86, Precision::FP32>());
    ModelGraph->load(model_name);
#ifdef ENABLE_DEBUG
    printf("model loaded\n");
#endif
    ModelGraph->PrintStructure();
    //ModelGraph->ResetBatchSize("input_0", 50);
    ModelGraph->Optimize();
#ifdef ENABLE_DEBUG
    printf("model optimized\n");
#endif

    ModelNet.reset(new Net<X86, Precision::FP32>(*ModelGraph, true));

    return 0;
}

extern "C" int seal_data(size_t input_size, const void *input,
                         size_t output_max_size, void *output,
                         size_t *result_size) {
    uint32_t output_len = sgx_calc_sealed_data_size(0, input_size);

    if (output_len > output_max_size) return -1;

    auto rc = sgx_seal_data(0, NULL, input_size, static_cast<const uint8_t *>(input),
                            output_len, static_cast<sgx_sealed_data_t *>(output));

    if (rc != SGX_SUCCESS) return -2;

    *result_size = output_len;

    return 0;
}

extern "C" int unseal_data(size_t input_size, const void *input,
                           size_t output_max_size, void *output,
                           size_t *result_size) {
    auto sealed_data = static_cast<const sgx_sealed_data_t *>(input);
    uint32_t input_len = sgx_get_encrypt_txt_len(sealed_data);

    if (input_len > output_max_size) return -1;

    uint32_t mac_length = 0;
    auto rc = sgx_unseal_data(sealed_data, NULL, &mac_length,
                              static_cast<uint8_t *>(output), &input_len);

    if (rc != SGX_SUCCESS) return -2;

    *result_size = input_len;

    return 0;
}


/*
extern "C" int enc_infer(size_t has_input, const char *input_filename, const char *output_filename) {

    if (!ModelNet) return -1;

    SGX_FILE* fp;
    size_t input_size;
    uint8_t sgx_input[BUFLEN];
    if(!input_filename){
        input_size=0;
    }
    else{
        fp = sgx_fopen(input_filename, "r", (sgx_key_128bit_t *)key);
        if (!fp) {
            printf("error: cannot open input file\n");
            return -1;
        }
        sgx_fseek(fp, 0, SEEK_END);
        uint64_t fend = sgx_ftell(fp);
        sgx_fseek(fp, 0, SEEK_SET);
        if (fend > sizeof(sgx_input)){
	    printf("error: oversized input!\n");
	    return -1;
        }
        if (fend <= 0) {
            printf("error: cannot read input file\n");
            return -1;
        }
        input_size = fend;
        if (input_size != sgx_fread(sgx_input, 1, input_size, fp)) {
            printf("error: cannot read input file\n");
            return -1;
        }
        sgx_fclose(fp);
    }

    // Check input size requirement
    if (input_size != 0) {
        auto h_in = ModelNet->get_in_list().at(0);
        auto input_tensor_size = h_in->get_dtype_size() * h_in->valid_size();
        if (input_size != input_tensor_size) return -2;
    }
    
    // Check output size requirement
    auto h_out = ModelNet->get_out_list().at(0);
    auto output_tensor_size = h_out->get_dtype_size() * h_out->valid_size();
    if (output_tensor_size > BUFLEN) return -3;

    if (input_size == 0) {
        for (auto h_in : ModelNet->get_in_list()) {
            fill_tensor_const(*h_in, 1);
        }
    } else {
        auto start = static_cast<const float *>((void *)sgx_input);
        for (auto h_in : ModelNet->get_in_list()) {
            auto end = start + h_in->valid_size();
            std::copy(start, end, static_cast<float *>(h_in->data()));
            start = end;
        }
    }

    ModelNet->prediction();
    mkl_free_buffers();

    auto p_float = static_cast<const float *>(h_out->data());

#ifdef ENABLE_DEBUG
    auto c = h_out->valid_size();
    for (int i = 0; i < c; i++) {
        float f = p_float[i];
        printf("%f\n", f);
    }
#endif

    std::copy(p_float, p_float + h_out->valid_size(), static_cast<float *>((void *)sgx_input));

    size_t result_size = output_tensor_size;

    fp = sgx_fopen(output_filename, "w", (sgx_key_128bit_t *)key);
    if (!fp) {
        printf("error: cannot open output file\n");
        return -1;
    }
    if (result_size != sgx_fwrite(sgx_input, 1, result_size, fp)) {
            printf("error: cannot write output file\n");
            return -1;
    }
    sgx_fclose(fp);

    return 0;
}*/

extern "C" int infer(size_t input_size, const void *input,
                     size_t output_max_size, void *output,
                     size_t *result_size) {

    if (!ModelNet) return -1;

    uint8_t p_dst[BUFLEN] = {0};


    // Check input size requirement
    if (input_size != 0) {
        //decrypt and change input size
	size_t actual_input_size = input_size;
        #ifdef USE_ENCRYPTION
        actual_input_size = input_size - SGX_AESGCM_MAC_SIZE - SGX_AESGCM_IV_SIZE;
	//printf("before decryption\n");
        sgx_rijndael128GCM_decrypt(
		&key,
		input + SGX_AESGCM_MAC_SIZE + SGX_AESGCM_IV_SIZE,
		actual_input_size,
		p_dst,
		input + SGX_AESGCM_MAC_SIZE, SGX_AESGCM_IV_SIZE,
		NULL, 0,
		(sgx_aes_gcm_128bit_tag_t *) input);
	//memcpy(input, p_dst, actual_input_size);
	#endif


        auto h_in = ModelNet->get_in_list().at(0);
        auto input_tensor_size = h_in->get_dtype_size() * h_in->valid_size();
        if (actual_input_size != input_tensor_size) return -2;
    }
    
    // Check output size requirement
    auto h_out = ModelNet->get_out_list().at(0);
    auto output_tensor_size = h_out->get_dtype_size() * h_out->valid_size();
    if (output_tensor_size > output_max_size) return -3;

    if (input_size == 0) {
        for (auto h_in : ModelNet->get_in_list()) {
            fill_tensor_const(*h_in, 1);
        }
    } else {
        //auto start = static_cast<const float *>(input);
	auto start = static_cast<const float *>((float *)p_dst);
        for (auto h_in : ModelNet->get_in_list()) {
            auto end = start + h_in->valid_size();
            std::copy(start, end, static_cast<float *>(h_in->data()));
            start = end;
        }
    }

    for(int i=0;i<1;i++){
        ModelNet->prediction();
	mkl_free_buffers();
    }
    //printf("infer finished\n");
    auto p_float = static_cast<const float *>(h_out->data());

#ifdef ENABLE_DEBUG_2
    auto c = h_out->valid_size();
    for (int i = 0; i < c; i++) {
        float f = p_float[i];
        printf("%f\n", f);
    }
#endif

    std::copy(p_float, p_float + h_out->valid_size(), static_cast<float *>(output));

    *result_size = output_tensor_size;
    #ifdef USE_ENCRYPTION
    *result_size += SGX_AESGCM_MAC_SIZE + SGX_AESGCM_IV_SIZE;
    //printf("before encryption\n");
    //uint8_t p_dst2[BUFLEN] = {0};

	// Generate the IV (nonce)
    sgx_read_rand(p_dst + SGX_AESGCM_MAC_SIZE, SGX_AESGCM_IV_SIZE);

    sgx_rijndael128GCM_encrypt(
    	&key,
	(uint8_t *)output, output_tensor_size,
	p_dst + SGX_AESGCM_MAC_SIZE + SGX_AESGCM_IV_SIZE,
	p_dst + SGX_AESGCM_MAC_SIZE, SGX_AESGCM_IV_SIZE,
	NULL, 0,
	(sgx_aes_gcm_128bit_tag_t *) (p_dst));
    memcpy(output, p_dst, output_tensor_size + SGX_AESGCM_MAC_SIZE + SGX_AESGCM_IV_SIZE);

    #endif
    return 0;
}

}
