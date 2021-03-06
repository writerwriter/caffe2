/**
 * Copyright (c) 2016-present, Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "caffe2/core/init.h"
#include "caffe2/core/net.h"
#include "caffe2/core/operator.h"
#include "caffe2/mpi/mpi_common.h"
#include "google/protobuf/text_format.h"
#include <gtest/gtest.h>

CAFFE2_DEFINE_string(
    caffe_test_root, "gen/", "The root of the caffe test folder.");

namespace caffe2 {

const char kBcastNet[] = R"NET(
  name: "bcast"
  op {
    output: "comm"
    type: "MPICreateCommonWorld"
  }
  op {
    output: "X"
    type: "ConstantFill"
    arg {
      name: "shape"
      ints: 10
    }
    arg {
      name: "value"
      f: 0.0
    }
  }
  op {
    input: "comm"
    input: "X"
    output: "X"
    type: "MPIBroadcast"
    arg {
      name: "root"
      i: 0
    }
  }
)NET";

TEST(MPITest, TestMPIBroadcast) {
  NetDef net_def;
  CHECK(google::protobuf::TextFormat::ParseFromString(
      string(kBcastNet), &net_def));
  // Let's set the network's constant fill value to be the mpi rank.
  auto* arg = net_def.mutable_op(1)->mutable_arg(1);
  CAFFE_ENFORCE_EQ(arg->name(), "value");
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  arg->set_f(rank);
  int size;
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  for (int root = 0; root < size; ++root) {
    net_def.mutable_op(2)->mutable_arg(0)->set_i(root);
    Workspace ws;
    unique_ptr<NetBase> net(CreateNet(net_def, &ws));
    EXPECT_NE(nullptr, net.get());
    EXPECT_TRUE(net->Run());
    // Let's test the value.
    auto& X = ws.GetBlob("X")->Get<TensorCPU>();
    EXPECT_EQ(X.size(), 10);
    for (int i = 0; i < X.size(); ++i) {
      EXPECT_EQ(X.data<float>()[i], root);
    }
  }
}

const char kReduceNet[] = R"NET(
  name: "reduce"
  op {
    output: "comm"
    type: "MPICreateCommonWorld"
  }
  op {
    output: "X"
    type: "ConstantFill"
    arg {
      name: "shape"
      ints: 10
    }
    arg {
      name: "value"
      f: 0.0
    }
  }
  op {
    input: "comm"
    input: "X"
    output: "X_reduced"
    type: "MPIReduce"
    arg {
      name: "root"
      i: 0
    }
  }
)NET";

TEST(MPITest, TestMPIReduce) {
  NetDef net_def;
  CHECK(google::protobuf::TextFormat::ParseFromString(
      string(kReduceNet), &net_def));
  // Let's set the network's constant fill value to be the mpi rank.
  auto* arg = net_def.mutable_op(1)->mutable_arg(1);
  CAFFE_ENFORCE_EQ(arg->name(), "value");
  int rank0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank0);
  arg->set_f(rank0);
  int size0;
  MPI_Comm_size(MPI_COMM_WORLD, &size0);

  for (int root = 0; root < size0; ++root) {
    net_def.mutable_op(2)->mutable_arg(0)->set_i(root);
    Workspace ws;
    unique_ptr<NetBase> net(CreateNet(net_def, &ws));
    EXPECT_NE(nullptr, net.get());
    EXPECT_TRUE(net->Run());
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    int size;
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    if (rank == root) {
      // Let's test the value.
      auto& X = ws.GetBlob("X_reduced")->Get<TensorCPU>();
      EXPECT_EQ(X.size(), 10);
      int expected_result = size * (size - 1) / 2;
      for (int i = 0; i < X.size(); ++i) {
        EXPECT_EQ(X.data<float>()[i], expected_result);
      }
    }
  }
}

const char kMPIAllgatherNet[] = R"NET(
  name: "allgather"
  op {
    output: "comm"
    type: "MPICreateCommonWorld"
  }
  op {
    output: "X"
    type: "ConstantFill"
    arg {
      name: "shape"
      ints: 2
      ints: 10
    }
    arg {
      name: "value"
      f: 0.0
    }
  }
  op {
    input: "comm"
    input: "X"
    output: "X_gathered"
    type: "MPIAllgather"
  }
)NET";

TEST(MPITest, TestMPIAllgather) {
  NetDef net_def;
  CHECK(google::protobuf::TextFormat::ParseFromString(
      string(kMPIAllgatherNet), &net_def));
  // Let's set the network's constant fill value to be the mpi rank.
  auto* arg = net_def.mutable_op(1)->mutable_arg(1);
  CAFFE_ENFORCE_EQ(arg->name(), "value");
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  arg->set_f(rank);
  int size;
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  Workspace ws;
  unique_ptr<NetBase> net(CreateNet(net_def, &ws));
  EXPECT_NE(nullptr, net.get());
  EXPECT_TRUE(net->Run());
  // Let's test the value.
  auto& X = ws.GetBlob("X")->Get<TensorCPU>();
  EXPECT_EQ(X.size(), 20);
  for (int i = 0; i < X.size(); ++i) {
    EXPECT_EQ(X.data<float>()[i], rank);
  }
  auto& X_gathered = ws.GetBlob("X_gathered")->Get<TensorCPU>();
  EXPECT_EQ(X_gathered.size(), 20 * size);
  EXPECT_EQ(X_gathered.dim(0), 2 * size);
  EXPECT_EQ(X_gathered.dim(1), 10);
  for (int i = 0; i < X_gathered.size(); ++i) {
    EXPECT_EQ(X_gathered.data<float>()[i], i / 20);
  }
}

const char kMPIAllreduceNet[] = R"NET(
  name: "allreduce"
  op {
    output: "comm"
    type: "MPICreateCommonWorld"
  }
  op {
    output: "X"
    type: "ConstantFill"
    arg {
      name: "shape"
      ints: 10
    }
    arg {
      name: "value"
      f: 0.0
    }
  }
  op {
    input: "comm"
    input: "X"
    output: "X_reduced"
    type: "MPIAllreduce"
  }
)NET";

TEST(MPITest, TestMPIAllreduce) {
  NetDef net_def;
  CHECK(google::protobuf::TextFormat::ParseFromString(
      string(kMPIAllreduceNet), &net_def));
  // Let's set the network's constant fill value to be the mpi rank.
  auto* arg = net_def.mutable_op(1)->mutable_arg(1);
  CAFFE_ENFORCE_EQ(arg->name(), "value");
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  arg->set_f(rank);
  int size;
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  Workspace ws;
  unique_ptr<NetBase> net(CreateNet(net_def, &ws));
  EXPECT_NE(nullptr, net.get());
  EXPECT_TRUE(net->Run());
  // Let's test the value.
  auto& X = ws.GetBlob("X")->Get<TensorCPU>();
  EXPECT_EQ(X.size(), 10);
  for (int i = 0; i < X.size(); ++i) {
    EXPECT_EQ(X.data<float>()[i], rank);
  }
  auto& X_reduced = ws.GetBlob("X_reduced")->Get<TensorCPU>();
  EXPECT_EQ(X_reduced.size(), 10);
  int expected_result = size * (size - 1) / 2;
  for (int i = 0; i < X_reduced.size(); ++i) {
    EXPECT_EQ(X_reduced.data<float>()[i], expected_result);
  }
}

const char kInPlaceMPIAllreduceNet[] = R"NET(
  name: "allreduce"
  op {
    output: "comm"
    type: "MPICreateCommonWorld"
  }
  op {
    output: "X"
    type: "ConstantFill"
    arg {
      name: "shape"
      ints: 10
    }
    arg {
      name: "value"
      f: 0.0
    }
  }
  op {
    input: "comm"
    input: "X"
    output: "X"
    type: "MPIAllreduce"
  }
)NET";

TEST(MPITest, TestInPlaceMPIAllreduce) {
  NetDef net_def;
  CHECK(google::protobuf::TextFormat::ParseFromString(
      string(kInPlaceMPIAllreduceNet), &net_def));
  // Let's set the network's constant fill value to be the mpi rank.
  auto* arg = net_def.mutable_op(1)->mutable_arg(1);
  CAFFE_ENFORCE_EQ(arg->name(), "value");
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  arg->set_f(rank);
  int size;
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  Workspace ws;
  unique_ptr<NetBase> net(CreateNet(net_def, &ws));
  EXPECT_NE(nullptr, net.get());
  EXPECT_TRUE(net->Run());
  auto& X_reduced = ws.GetBlob("X")->Get<TensorCPU>();
  EXPECT_EQ(X_reduced.size(), 10);
  int expected_result = size * (size - 1) / 2;
  for (int i = 0; i < X_reduced.size(); ++i) {
    EXPECT_EQ(X_reduced.data<float>()[i], expected_result);
  }
}

}  // namespace caffe2


GTEST_API_ int main(int argc, char **argv) {
  int mpi_ret;
  MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &mpi_ret);
  testing::InitGoogleTest(&argc, argv);
  caffe2::GlobalInit(&argc, &argv);
  int test_result = RUN_ALL_TESTS();
  MPI_Finalize();
  return test_result;
}
