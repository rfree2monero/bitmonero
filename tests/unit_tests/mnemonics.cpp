// Copyright (c) 2014-2015, The Monero Project
// 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
// 
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "gtest/gtest.h"
#include "mnemonics/electrum-words.h"
#include "crypto/crypto.h"
#include <stdlib.h>
#include <vector>
#include <time.h>
#include <iostream>
#include <boost/algorithm/string.hpp>
#include "mnemonics/english.h"
#include "mnemonics/spanish.h"
#include "mnemonics/portuguese.h"
#include "mnemonics/japanese.h"
#include "mnemonics/old_english.h"
#include "mnemonics/language_base.h"
#include "mnemonics/singleton.h"

namespace
{
  /*!
   * \brief Returns random index from 0 to max-1
   * \param  max Range maximum
   * \return     required random index
   */
  uint32_t get_random_index(int max)
  {
    return rand() % max;
  }

  /*!
   * \brief Compares vectors for equality
   * \param expected expected vector
   * \param present  current vector
   */
  void compare_vectors(const std::vector<std::string> &expected, const std::vector<std::string> &present)
  {
    std::vector<std::string>::const_iterator it1, it2;
    for (it1 = expected.begin(), it2 = present.begin(); it1 != expected.end() && it2 != present.end();
      it1++, it2++)
    {
      ASSERT_STREQ(it1->c_str(), it2->c_str());
    }
  }

  /*!
   * \brief Tests the given language mnemonics.
   * \param language A Language instance to test
   */
  void test_language(const Language::Base &language)
  {
    const std::vector<std::string> &word_list = language.get_word_list();
    std::string seed = "", return_seed = "";
    // Generate a random seed without checksum
    for (int ii = 0; ii < crypto::ElectrumWords::seed_length; ii++)
    {
      seed += (word_list[get_random_index(word_list.size())] + ' ');
    }
    seed.pop_back();
    std::cout << "Test seed without checksum:\n";
    std::cout << seed << std::endl;

    crypto::secret_key key;
    std::string language_name;
    bool res;
    std::vector<std::string> seed_vector, return_seed_vector;
    std::string checksum_word;

    // Convert it to secret key
    res = crypto::ElectrumWords::words_to_bytes(seed, key, language_name);
    ASSERT_EQ(true, res);
    std::cout << "Detected language: " << language_name << std::endl;
    ASSERT_STREQ(language.get_language_name().c_str(), language_name.c_str());

    // Convert the secret key back to seed
    crypto::ElectrumWords::bytes_to_words(key, return_seed, language.get_language_name());
    ASSERT_EQ(true, res);
    std::cout << "Returned seed:\n";
    std::cout << return_seed << std::endl;
    boost::split(seed_vector, seed, boost::is_any_of(" "));
    boost::split(return_seed_vector, return_seed, boost::is_any_of(" "));

    // Extract the checksum word
    checksum_word = return_seed_vector.back();
    return_seed_vector.pop_back();
    ASSERT_EQ(seed_vector.size(), return_seed_vector.size());
    // Ensure that the rest of it is same
    compare_vectors(seed_vector, return_seed_vector);

    // Append the checksum word to repeat the entire process with a seed with checksum
    seed += (" " + checksum_word);
    std::cout << "Test seed with checksum:\n";
    std::cout << seed << std::endl;
    res = crypto::ElectrumWords::words_to_bytes(seed, key, language_name);
    ASSERT_EQ(true, res);
    std::cout << "Detected language: " << language_name << std::endl;
    ASSERT_STREQ(language.get_language_name().c_str(), language_name.c_str());

    return_seed = "";
    crypto::ElectrumWords::bytes_to_words(key, return_seed, language.get_language_name());
    ASSERT_EQ(true, res);
    std::cout << "Returned seed:\n";
    std::cout << return_seed << std::endl;

    seed_vector.clear();
    return_seed_vector.clear();
    boost::split(seed_vector, seed, boost::is_any_of(" "));
    boost::split(return_seed_vector, return_seed, boost::is_any_of(" "));
    ASSERT_EQ(seed_vector.size(), return_seed_vector.size());
    compare_vectors(seed_vector, return_seed_vector);
  }
}

TEST(mnemonics, all_languages)
{
  srand(time(NULL));
  std::vector<Language::Base*> languages({
    Language::Singleton<Language::English>::instance(),
    Language::Singleton<Language::Spanish>::instance(),
    Language::Singleton<Language::Portuguese>::instance(),
    Language::Singleton<Language::Japanese>::instance(),
  });

  for (std::vector<Language::Base*>::iterator it = languages.begin(); it != languages.end(); it++)
  {
    test_language(*(*it));
  }
}
