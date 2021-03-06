/*
 * WALogin.cpp
 *
 *  Created on: 26/06/2012
 *      Author: Antonio
 */

#include "WALogin.h"
#include "ByteArray.h"
#include "ApplicationData.h"
#include "ProtocolTreeNode.h"
#include "WAException.h"
#include "base64.h"
#include <iostream>
#include <vector>
#include <map>
#include <stdlib.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>


using namespace Utilities;

const std::string WALogin::NONCE_KEY = "nonce=\"";

WALogin::WALogin(WAConnection* connection, BinTreeNodeReader *reader, BinTreeNodeWriter *writer, const std::string& domain, const std::string& user, const std::string& resource, const std::string& password, const std::string& push_name) {
	this->connection = connection;
	this->inn = reader;
	this->out = writer;
	this->domain = domain;
	this->user = user;
	this->resource = resource;
	this->password = password;
	this->push_name = push_name;
	this->supports_receipt_acks = false;
	this->account_kind = -1;
	this->expire_date = 0L;
	this->outputKey = NULL;
}

std::vector<unsigned char>* WALogin::login(const std::vector<unsigned char>& authBlob) {
	this->out->streamStart(this->domain, this->resource);

	_LOGDATA("sent stream start");

	sendFeatures();

	_LOGDATA("sent features");

	sendAuth(authBlob);

	_LOGDATA("send auth, auth blob size %d", authBlob.size());

	this->inn->streamStart();

	_LOGDATA("read stream start");

	return this->readFeaturesUntilChallengeOrSuccess();
}

BinTreeNodeReader* WALogin::getTreeNodeReader() {
	return this->inn;
}

BinTreeNodeWriter* WALogin::getTreeNodeWriter() {
	return this->out;
}

std::string WALogin::getResponse(const std::string& challenge) {
	unsigned char md5_buffer[MD5_DIGEST_SIZE];

	size_t i = challenge.find(WALogin::NONCE_KEY);
	i += WALogin::NONCE_KEY.length();

	size_t j = challenge.find('"', i);
	std::string nonce = challenge.substr(i,j-i);

	std::string cnonce = str(absLong(randLong()), 36);
	_LOGDATA("cnonce = %s", cnonce.c_str());
	std::string nc = "00000001";
	std::string cinfo(this->user + ":" + this->domain + ":" + this->password);

	_LOGDATA("cinfo = %s", cinfo.c_str());

	ByteArrayOutputStream bos;
	_LOGDATA((char*) md5digest((unsigned char*) cinfo.data(), cinfo.length(), md5_buffer), MD5_DIGEST_SIZE);
	bos.write(md5digest((unsigned char*) cinfo.data(), cinfo.length(), md5_buffer), MD5_DIGEST_SIZE);
	bos.write(58);
	bos.write(nonce);
	bos.write(58);
	bos.write(cnonce);
	// bos.print();

	std::string digest_uri = "xmpp/" + this->domain;
	std::vector<unsigned char>* A1 = bos.toByteArray();
	std::string A2 = "AUTHENTICATE:" + digest_uri;
	std::string KD((char*) bytesToHex(md5digest(&A1->front(), A1->size(), md5_buffer), MD5_DIGEST_SIZE), MD5_DIGEST_SIZE * 2);
	KD += + ":" + nonce + ":" + nc + ":" + cnonce + ":auth:" + std::string((char*) bytesToHex(md5digest((unsigned char*) A2.data(), A2.size(), md5_buffer), MD5_DIGEST_SIZE), MD5_DIGEST_SIZE*2);

	_LOGDATA("KD = %s", KD.c_str());

	std::string response((char*) bytesToHex(md5digest((unsigned char*) KD.data(), KD.size(), md5_buffer), MD5_DIGEST_SIZE), MD5_DIGEST_SIZE*2);

	_LOGDATA("response = %s", response.c_str());

	std::string bigger_response;
	bigger_response.append("realm=\"");
	bigger_response.append(this->domain);
	bigger_response.append("\",response=");
	bigger_response.append(response);
	bigger_response.append(",nonce=\"");
	bigger_response.append(nonce);
	bigger_response.append("\",digest-uri=\"");
	bigger_response.append(digest_uri);
	bigger_response.append("\",cnonce=\"");
	bigger_response.append(cnonce);
	bigger_response.append("\",qop=auth");
	bigger_response.append(",username=\"");
	bigger_response.append(this->user);
	bigger_response.append("\",nc=");
	bigger_response.append(nc);

	_LOGDATA("biggerresponse = %s", bigger_response.c_str());

	delete A1;

	return bigger_response;
}

std::string WALogin::getAuthoritationString(const std::string& user, const std::string& password, const std::string& nonce) {
	unsigned char md5_buffer[MD5_DIGEST_SIZE];
	std::string domain = "s.whatsapp.net";

	std::string cnonce = str(absLong(randLong()), 36);
	_LOGDATA("cnonce = %s", cnonce.c_str());
	std::string nc = "00000001";

	std::string cinfo(user + ":" + domain + ":" + base64_decode(password));

	_LOGDATA("cinfo = %s", cinfo.c_str());

	ByteArrayOutputStream bos;
	_LOGDATA((char*) md5digest((unsigned char*) cinfo.data(), cinfo.length(), md5_buffer), MD5_DIGEST_SIZE);
	bos.write(md5digest((unsigned char*) cinfo.data(), cinfo.length(), md5_buffer), MD5_DIGEST_SIZE);
	bos.write(58);
	bos.write(nonce);
	bos.write(58);
	bos.write(cnonce);
	// bos.print();

	std::string digest_uri = "WAWA/" + domain;
	std::vector<unsigned char>* A1 = bos.toByteArray();
	std::string A2 = "AUTHENTICATE:" + digest_uri;
	std::string KD((char*) bytesToHex(md5digest(&A1->front(), A1->size(), md5_buffer), MD5_DIGEST_SIZE), MD5_DIGEST_SIZE * 2);
	KD += + ":" + nonce + ":" + nc + ":" + cnonce + ":auth:" + std::string((char*) bytesToHex(md5digest((unsigned char*) A2.data(), A2.size(), md5_buffer), MD5_DIGEST_SIZE), MD5_DIGEST_SIZE*2);

	_LOGDATA("KD = %s", KD.c_str());

	std::string response((char*) bytesToHex(md5digest((unsigned char*) KD.data(), KD.size(), md5_buffer), MD5_DIGEST_SIZE), MD5_DIGEST_SIZE*2);

	_LOGDATA("response = %s", response.c_str());

	std::string bigger_response;
	bigger_response.append("X-WAWA: ");
	bigger_response.append("username=\"");
	bigger_response.append(user);
	bigger_response.append("\",realm=\"");
	bigger_response.append(domain);
	bigger_response.append("\",nonce=\"");
	bigger_response.append(nonce);
	bigger_response.append("\",cnonce=\"");
	bigger_response.append(cnonce);
	bigger_response.append("\",nc=\"");
	bigger_response.append(nc);
	bigger_response.append("\",qop=\"auth");
	bigger_response.append("\",digest-uri=\"");
	bigger_response.append(digest_uri);
	bigger_response.append("\",response=\"");
	bigger_response.append(response);
	bigger_response.append("\",charset=\"utf-8\"");

	_LOGDATA("biggerresponse = %s", bigger_response.c_str());

	delete A1;

	return bigger_response;
}

void WALogin::sendResponse(const std::vector<unsigned char>& challengeData) {
	std::vector<unsigned char>* authBlob = this->getAuthBlob(challengeData);

	// std::string response = this->getResponse(challengeData);
	std::map<string, string> *attributes = new std::map<string,string>();
	(*attributes)["xmlns"] = "urn:ietf:params:xml:ns:xmpp-sasl";
	ProtocolTreeNode node("response", attributes, authBlob);

	this->out->write(&node);
}

void WALogin::sendFeatures() {
	ProtocolTreeNode* child = new ProtocolTreeNode("receipt_acks", NULL);
	std::vector<ProtocolTreeNode*>* children = new std::vector<ProtocolTreeNode*>();
	children->push_back(child);

	std::map<string, string>* attributes = new std::map<string, string>();
	(*attributes)["type"] = "all";
	ProtocolTreeNode* pictureChild = new ProtocolTreeNode("w:profile:picture", attributes);
	 children->push_back(pictureChild);

	// children->push_back(new ProtocolTreeNode("status", NULL));

	ProtocolTreeNode node("stream:features", NULL, NULL, children);
	this->out->write(&node, true);
}

void WALogin::sendAuth(const std::vector<unsigned char>& existingChallenge) {
	std::vector<unsigned char>* data = NULL;
	if (!existingChallenge.empty()) {
		data = this->getAuthBlob(existingChallenge);
	}

	std::map<string, string>* attributes = new std::map<string, string>();
	(*attributes)["xmlns"] = "urn:ietf:params:xml:ns:xmpp-sasl";
	(*attributes)["mechanism"] = "WAUTH-1";
	(*attributes)["user"] = this->user;

	ProtocolTreeNode node("auth", attributes, data, NULL);
	this->out->write(&node, true);
}

std::vector<unsigned char>* WALogin::getAuthBlob(const std::vector<unsigned char>& nonce) {
	unsigned char out[20];
	KeyStream::keyFromPasswordAndNonce(this->password, nonce, out);

	if (this->connection->inputKey != NULL)
		delete this->connection->inputKey;
	this->connection->inputKey = new KeyStream(out, 20);

	if (this->outputKey != NULL)
		delete this->outputKey;

	this->outputKey = new KeyStream(out, 20);
	std::vector<unsigned char>* list = new std::vector<unsigned char>(0);
	for (int i = 0; i < 4; i++) {
		list->push_back(0);
	}
	list->insert(list->end(), this->user.begin(), this->user.end());
	list->insert(list->end(), nonce.begin(), nonce.end());
	time_t now;
	std::string time = Utilities::intToStr(difftime(mktime(gmtime(&now)), 0));
	list->insert(list->end(), time.begin(), time.end());

	this->outputKey->encodeMessage(&((*list)[0]), 0, 4, list->size() - 4);
	return list;
}

std::vector<unsigned char>* WALogin::readFeaturesUntilChallengeOrSuccess() {
	ProtocolTreeNode* root;
	while ((root = this->inn->nextTree()) != NULL) {
		if (ProtocolTreeNode::tagEquals(root, "stream:features")) {
			this->supports_receipt_acks = root->getChild("receipt_acks") != NULL;
			delete root;
			continue;
		}
		if (ProtocolTreeNode::tagEquals(root, "challenge")) {
			// base64_decode(*root->data);
			// _LOGDATA("Challenge data %s (%d)", root->data->c_str(), root->data->length());
			std::vector<unsigned char> challengedata(root->data->begin(), root->data->end());
			delete root;
			this->sendResponse(challengedata);
			_LOGDATA("Send response");
			std::vector<unsigned char> data = this->readSuccess();
			_LOGDATA("Read success");
			return new std::vector<unsigned char>(data.begin(), data.end());
		}
		if (ProtocolTreeNode::tagEquals(root, "success")) {
			// base64_decode(*root->data);
			std::vector<unsigned char>* ret = new std::vector<unsigned char>(root->data->begin(), root->data->end());
			this->parseSuccessNode(root);
			delete root;
			return ret;
		}
	}
	throw WAException("fell out of loop in readFeaturesAndChallenge", WAException::CORRUPT_STREAM_EX, 0);
}

void WALogin::parseSuccessNode(ProtocolTreeNode* node) {
	std::string* expiration = node->getAttributeValue("expiration");
	if (expiration != NULL) {
		this->expire_date = atol(expiration->c_str());
		if (this->expire_date == 0)
			throw WAException("invalid expire date: " + *expiration);
	}


	std::string* kind = node->getAttributeValue("kind");
	if (kind != NULL && kind->compare("paid") == 0)
		this->account_kind = 1;
	else if (kind != NULL && kind->compare("free") == 0)
		this->account_kind = 0;
	else
		this->account_kind = -1;

	if (this->connection->outputKey != NULL)
		delete this->connection->outputKey;
	this->connection->outputKey = this->outputKey;
}

std::vector<unsigned char> WALogin::readSuccess() {
	ProtocolTreeNode* node = this->inn->nextTree();

	if (ProtocolTreeNode::tagEquals(node, "failure")) {
		delete node;
		throw WAException("Login failure", WAException::LOGIN_FAILURE_EX, WAException::LOGIN_FAILURE_EX_TYPE_PASSWORD);
	}

	ProtocolTreeNode::require(node, "success");
	this->parseSuccessNode(node);

	std::string* status = node->getAttributeValue("status");
	if (status != NULL && status->compare("expired") == 0) {
		delete node;
		throw WAException("Account expired on" + std::string(ctime(&this->expire_date)), WAException::LOGIN_FAILURE_EX, WAException::LOGIN_FAILURE_EX_TYPE_EXPIRED, this->expire_date);
	}
	if (status != NULL && status->compare("active") == 0) {
		if (node->getAttributeValue("expiration") == NULL) {
			delete node;
			throw WAException("active account with no expiration");
		}
	} else
		this->account_kind = -1;

	std::vector<unsigned char> data = *node->data;
	delete node;
	return data;
}

WALogin::~WALogin() {
	if (this->inn != NULL)
		delete inn;
	if (this->out != NULL)
		delete out;
}

KeyStream::KeyStream(unsigned char* key, size_t keyLength) {
	unsigned char drop[256];

	this->key = new unsigned char[keyLength];
	memcpy(this->key, key, keyLength);
	this->keyLength = keyLength;

	RC4_set_key(&this->rc4, this->keyLength, this->key);
	RC4(&this->rc4, 256, drop, drop);
	HMAC_CTX_init(&this->hmac);
}

KeyStream::~KeyStream() {
	delete [] this->key;
	HMAC_CTX_cleanup(&this->hmac);
}

void KeyStream::keyFromPasswordAndNonce(const std::string& pass, const std::vector<unsigned char>& nonce, unsigned char *out) {
	PKCS5_PBKDF2_HMAC_SHA1(pass.data(), pass.size(), nonce.data(), nonce.size(), 16, 20, out);
}

void KeyStream::decodeMessage(unsigned char* buffer, int macOffset, int offset, int length) {
	unsigned char digest[20];
	this->hmacsha1(buffer + offset, length, digest);

	for (int i = 0; i < 4; i++) {
		if (buffer[macOffset + i] != digest[i]) {
			throw WAException("invalid MAC", WAException::CORRUPT_STREAM_EX, 0);
		}
	}
	unsigned char out[length];
	RC4(&this->rc4, length, buffer + offset, out);
	memcpy(buffer + offset, out, length);
}

void KeyStream::encodeMessage(unsigned char* buffer, int macOffset, int offset, int length) {
	unsigned char out[length];
	RC4(&this->rc4, length, buffer + offset, out);
	memcpy(buffer + offset, out, length);

	unsigned char digest[20];
	this->hmacsha1(buffer + offset, length, digest);

	memcpy(buffer + macOffset, digest, 4);
}

void KeyStream::hmacsha1(unsigned char* text, int textLength, unsigned char *out) {
	// CHMAC_SHA1 hmac;

	// hmac.HMAC_SHA1(text, textLength, this->key, this->keyLength, out);

	unsigned int mdLength;
	HMAC(EVP_sha1(), this->key, this->keyLength, text, textLength, out, &mdLength);
}




