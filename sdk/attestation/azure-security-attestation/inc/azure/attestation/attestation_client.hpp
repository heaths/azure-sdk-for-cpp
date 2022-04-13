// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

#pragma once

#include "azure/attestation/attestation_client_models.hpp"
#include "azure/attestation/attestation_client_options.hpp"
#include <azure/core/context.hpp>
#include <azure/core/url.hpp>
#include <string>

namespace Azure { namespace Core { namespace Http { namespace _internal {
  class HttpPipeline;
}}}} // namespace Azure::Core::Http::_internal

namespace Azure { namespace Security { namespace Attestation {

  /**
   *
   * The AttestationAsyncClient implements the functionality required by the "Attest" family of
   * APIs.
   *
   * An enclave (or Trusted Execution Environment) is a chunk of code that is isolated from the host
   * (think: "encrypted VM" or "encrypted container"). But there's one key attribute of the enclave:
   * It is encrypted.That means that
   * if data is sent from the enclave, there is no way of knowing that the data came from the
   * enclave. And even worse, there is no way of securely communicating with the enclave (since the
   * enclave is fully isolated from the host, all information passed into the enclave has to go
   * through its host first).
   *
   * To solve the communication problem, the Attest API can be used to facilitate what is
   * known as the "Secure Key Release" (SKR) protocol.
   *
   * There are 4 parties involved in an attestation operation:
   *
   * - The host (which hosts the enclave)
   * - The enclave (which is the enclave :) - encrypted, nobody can see what goes on inside it),
   * - The "verifier" which verifies the evidence from the enclave (this is the attestation service)
   * and generates a token which can be received by a relying party, and
   * - The "relying party" which will interpret the token from the service. For the Secure Key
   * Release Protocol, this is the entity which wishes to communicate with the enclave.
   *
   * It's possible that all these parties are on the same computer, it's possible they're on
   * multiple computers.<br> It's possible that the host is also the relying party. It's possible
   * that the relying party is a component like Azure Managed HSM.
   *
   * There are three primary pieces of data received by the service for the Attest family of APIs.
   * All of them are arrays of bytes, and all of them originate from code running in the enclave
   * (thus they need to be treated as opaque arrays of bytes by the SDK):
   *
   * -# Evidence. For Intel SGX enclaves, this has two forms, either an SGX 'Quote' or an
   * OpenEnclave 'Report'. It is required for attestation operations.
   * -# InitTimeData - This is data which is specified at Initialization Time. It is optional
   * (and not currently supported on all enclave types in Azure)
   * -# RunTimeData - this is data which is specified at the time the quote is generated (at
   * "runtime"). It is optional, but required for the Secure Key Release protocol.
   *
   * The Evidence is cryptographically signed by a known authority (for Intel SGX Quotes or
   * OpenEnclave reports, this is a key owned by Intel which represents that the SGX enclave is
   * valid and can be trusted).<br> The core idea for all attestation operations is to take
   * advantage of a region within the Evidence which is controlled by enclave. For SGX Enclaves,
   * this is the 64 bytes of "user data" contained within SGX quote.
   *
   * For the Secure Key Release protocol, code inside the enclave generates an asymmetric key and
   * serializes the public key into a byte buffer. It then calculates the SHA256 hash of the
   * serialized key and creates a quote containing that SHA256 hash. We now have a cryptographically
   * validated indication that the contents of the byte buffer was known inside the enclave.
   *
   * The enclave then hands the byte buffer and the quote to its host. The host sends the quote and
   * byte buffer as the "RunTime Data" to the via the AttestationClient.AttestSgxEnclave  or
   * AttestationClient.AttestOpenEnclave} API. Assuming the byte buffer and quote are
   * valid, and the quote contains the hash of the byte buffer, the attestation service responds
   * with an AttestationToken signed by the attestation service, whose body is an
   * AttestationResult.
   *
   * The token generated also includes the contents of the InitTimeData and/or RunTimeData if it was
   * provided in the Attest API call.
   *
   * The host then sends the token to the relying party.  The relying party verifies the token
   * and verifies the claims within the token indicate that the enclave is the correct enclave.
   * It then takes the key from the token and uses it to encrypt the data to be sent to the
   * enclave and sends that back to the host, which passes it into the enclave.
   *
   *
   * That completes the secure key release protocol.
   *
   * When the Attestation Token is generated by the attestation service, as mentioned, it contains
   * the InitTime and RunTime data.
   *
   * There are two possible representations for RunTime Data in the attestation token, depending on
   * the requirements of the relying party:<br> The first is as JSON formatted data. That can be
   * convenient if the relying party expects to receive its public key as a JSON Web Key The second
   * is as a binary blob of data. That is needed if either the data sent by the enclave isn't a JSON
   * object - for instance, if the RunTime data contained an asymmetric key which is formatted as a
   * PEM encoded key, it should be interpreted as a binary blob
   *
   * If you ask for the RunTime data to be included in the token as binary, then it will be
   * base64url encoded in the "x-ms-maa-enclavehelddata" claim in the output token (the
   * AttestationResult::EnclaveHeldData property).
   *
   * If you ask for the RunTime data to be included in the token as JSON, then it will be included
   * in the "x-ms-maa-runtimeClaims" claim in the output token (the AttestationResult::RuntimeClaims
   * property).
   *
   * In addition to the Attest APIs, the AttestationClient object also contains helper APIs
   * which can be used to retrieve the OpenId Metadata document and signing keys from the service.
   *
   * The OpenId Metadata document contains properties which describe the attestation service.
   *
   * The Attestation Signing Keys describe the keys which will be used to sign tokens generated by
   * the attestation service. All tokens emitted by the attestation service will be signed by one
   * of the certificates listed in the attestation signing keys.
   *
   */

  class AttestationClient final {
  public:
    /**
     * @brief Destructor.
     *
     */
    virtual ~AttestationClient() = default;

    /** @brief Construct a new Attestation Client object
     *
     * @param endpoint The URL address where the client will send the requests to.
     * @param credential The authentication method to use (required for TPM attestation).
     * @param options The options to customize the client behavior.
     */
    explicit AttestationClient(
        std::string const& endpoint,
        std::shared_ptr<Core::Credentials::TokenCredential const> credential,
        AttestationClientOptions options = AttestationClientOptions());

    /** @brief Construct a new anonymous Attestation Client object
     *
     * @param endpoint The URL address where the client will send the requests to.
     * @param options The options to customize the client behavior.
     *
     * @note TPM attestation requires an authenticated attestation client.
     */
    explicit AttestationClient(
        std::string const& endpoint,
        AttestationClientOptions options = AttestationClientOptions())
        : AttestationClient(endpoint, nullptr, options)
    {
    }

    /** @brief Construct a new Attestation Client object from an existing attestation client.
     *
     * @param attestationClient An existing attestation client.
     */
    explicit AttestationClient(AttestationClient const& attestationClient)
        : m_endpoint(attestationClient.m_endpoint), m_apiVersion(attestationClient.m_apiVersion),
          m_pipeline(attestationClient.m_pipeline),
          m_tokenValidationOptions(attestationClient.m_tokenValidationOptions){};

    /**
     * @brief Returns the API version the client was configured with.
     *
     * @returns The API version used when communicating with the attestation service.
     */
    std::string const& ClientVersion() const { return m_apiVersion; }

    /**
     * Retrieves metadata about the attestation signing keys in use by the attestation service.
     *
     * Retrieve the OpenID metadata for this attestation service instance..
     *
     * @return an \ref Models::AttestationOpenIdMetadata object containing metadata about the
     * specified service instance.
     */
    Response<Models::AttestationOpenIdMetadata> GetOpenIdMetadata(
        Azure::Core::Context const& context = Azure::Core::Context::ApplicationContext) const;

    /**
     * @brief Retrieve the attestation signing certificates for this attestation instance.
     *
     * @returns A Models::AttestationSigningCertificateResult containing a list of certificates one
     * of which will be used to validate tokens received by the attestation service.
     */
    Response<Models::AttestationSigningCertificateResult> GetAttestationSigningCertificates(
        Azure::Core::Context const& context = Azure::Core::Context{}) const;

    /**
     * @brief Retrieves the information needed to validate a response from the attestation service.
     *
     * @note: This method MUST be called before any calls to the attestation service which must be
     * validated.
     */
    void RetrieveResponseValidationCollateral(
        Azure::Core::Context const& context = Azure::Core::Context{}) const;

    /**
     * @brief Attest an SGX enclave, returning an attestation token representing the result
     * of the attestation operation.
     *
     * @param sgxQuoteToAttest - SGX Quote to be validated by the attestation service.
     * @param options - Options to the attestation request (runtime data, inittime data, etc).
     * @param context - Context for the operation.
     *
     * @returns Response<AttestationToken<AttestationResult>> - The result of the
     * attestation operation.
     *
     * @note \b Note: The RetrieveResponseValidationCollateral API \b MUST be called before the
     * AttestSgxEnclave API is called to retrieve the information needed to validate the
     * result returned by the service.
     */
    Response<Models::AttestationToken<Models::AttestationResult>> AttestSgxEnclave(
        std::vector<uint8_t> const& sgxQuoteToAttest,
        AttestOptions options = AttestOptions(),
        Azure::Core::Context const& context = Azure::Core::Context{}) const;

    /**
     * @brief Attest an OpenEnclave report, returning an attestation token representing the result
     * of the attestation operation.
     *
     * @param openEnclaveReportToAttest - OpenEnclave Report to be validated by the attestation
     * service.
     * @param options - Options to the attestation request (runtime data, inittime data, etc).
     * @param context - Context for the operation.
     *
     * @returns Response<AttestationToken<AttestationResult>> - The result of the attestation
     * operation

     * @note \b Note: The RetrieveResponseValidationCollateral API \b MUST be called before the
     * AttestOpenEnclave API is called to retrieve information needed to used to validate the
     * result returned by the service.
     */
    Response<Models::AttestationToken<Models::AttestationResult>> AttestOpenEnclave(
        std::vector<uint8_t> const& openEnclaveReportToAttest,
        AttestOptions options = AttestOptions(),
        Azure::Core::Context const& context = Azure::Core::Context{}) const;

    /**
    * @brief Perform a single leg
     *
     * Processes attestation evidence from a VBS enclave, producing an attestation result.
     *
     * The TPM attestation protocol is defined
    [here](https://docs.microsoft.com/azure/attestation/virtualization-based-security-protocol')
     *
     * Unlike OpenEnclave reports and SGX enclave quotes, TPM attestation is implemented using
     * JSON encoded strings.

     The client formats a string serialized JSON request to the
     * service, which responds with a JSON response. The serialized JSON object exchange continues
     * until the service responds with a JSON string with a property named {@code "report"}, whose
     * value will be an attestation result token.
     *
     * @param request Attestation request for Trusted Platform Module (TPM) attestation.
     * @return attestation response for Trusted Platform Module (TPM) attestation.
    */
    Response<std::string> AttestTpm(
        std::string const& jsonToSend,
        Azure::Core::Context const& context = Azure::Core::Context::ApplicationContext) const;

  private:
    Azure::Core::Url m_endpoint;
    std::string m_apiVersion;
    std::shared_ptr<Azure::Core::Credentials::TokenCredential const> m_credentials;
    std::shared_ptr<Azure::Core::Http::_internal::HttpPipeline> m_pipeline;
    AttestationTokenValidationOptions m_tokenValidationOptions;

    mutable std::vector<Models::AttestationSigner> m_attestationSigners;

    /**
     * @brief Check the m_AttestationSigners to ensure that RetrieveResponseValidationCollateral has
     * been called.
     */
    void CheckAttestationSigners() const;
  };

}}} // namespace Azure::Security::Attestation