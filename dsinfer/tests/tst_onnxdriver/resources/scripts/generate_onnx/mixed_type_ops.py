import os
from pathlib import Path
import onnx
from onnx import helper
from onnx import TensorProto


def current_directory():
    try:
        return Path(__file__).parent
    except NameError:
        return os.getcwd()


def create_model():
    N = 'N'

    # Define input tensors
    input_f1 = helper.make_tensor_value_info('input_f1', TensorProto.FLOAT, [1, N])
    input_f2 = helper.make_tensor_value_info('input_f2', TensorProto.FLOAT, [1, N])

    input_i1 = helper.make_tensor_value_info('input_i1', TensorProto.INT64, [1, N])
    input_i2 = helper.make_tensor_value_info('input_i2', TensorProto.INT64, [1, N])

    input_b1 = helper.make_tensor_value_info('input_b1', TensorProto.BOOL, [1, N])
    input_b2 = helper.make_tensor_value_info('input_b2', TensorProto.BOOL, [1, N])

    # Define output tensors
    output_f = helper.make_tensor_value_info('output_f', TensorProto.FLOAT, [1, N])
    output_i = helper.make_tensor_value_info('output_i', TensorProto.INT64, [1, N])
    output_b = helper.make_tensor_value_info('output_b', TensorProto.BOOL, [1, N])

    # Define computation nodes
    add_f_node = helper.make_node('Add', ['input_f1', 'input_f2'], ['output_f'], name='AddFloat')
    add_i_node = helper.make_node('Add', ['input_i1', 'input_i2'], ['output_i'], name='AddInt')
    xor_b_node = helper.make_node('Xor', ['input_b1', 'input_b2'], ['output_b'], name='XorBool')

    # Create the graph
    graph = helper.make_graph(
        [add_f_node, add_i_node, xor_b_node],
        'MixedTypeOpsGraph',
        [input_f1, input_f2, input_i1, input_i2, input_b1, input_b2],
        [output_f, output_i, output_b],
    )

    # Create the model
    model = helper.make_model(graph, producer_name='onnx-mixed-type-ops-example')

    return model

def save_onnx(model):
    # Save the model to a file
    model_path = current_directory().parents[1] / "models"
    model_filename = 'mixed_type_ops.onnx'
    onnx.save_model(model, model_path / model_filename)

    print("ONNX model 'mixed_type_ops.onnx' created successfully.")


if __name__ == '__main__':
    model = create_model()
    save_onnx(model)
