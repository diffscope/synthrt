"""Generates the duration models used by the Duration inference test.

A duration predictor declares the word inputs of the word level linguistic encoder only when it
consumes them, and the ONNX session rejects both a missing and an unexpected input. The models
generated here are much smaller than real ones and compute a value the test can predict exactly.
C{duration_word_encoder.onnx} is the word level linguistic encoder of every duration model.
C{duration_word_predictor.onnx} splits the frame budget of every word and takes both word inputs,
C{duration_division_predictor.onnx} locates the phonemes with the division and takes no frame
budget, and C{duration_absolute_predictor.onnx} predicts absolute durations and takes neither word
input.
"""
import os
from pathlib import Path

import onnx
from onnx import TensorProto, helper

N_TOKENS = 'n_tokens'
N_WORDS = 'n_words'


def current_directory():
    try:
        return Path(__file__).parent
    except NameError:
        return Path(os.getcwd())


def scalar_initializer(name, value, data_type=TensorProto.INT64):
    return helper.make_tensor(name, data_type, [], [value])


def make_model(graph):
    """Wraps C{graph} in a model of the operator set the fixtures are written against."""
    return helper.make_model(graph, producer_name='dsinfer-duration-auto-test',
                             opset_imports=[helper.make_opsetid('', 17)])


def make_encoder():
    """Builds the word level linguistic encoder.

    The encoder takes the tokens and the word structure of the score and reports the padding mask.
    """
    inputs = [
        helper.make_tensor_value_info('tokens', TensorProto.INT64, [1, N_TOKENS]),
        helper.make_tensor_value_info('word_div', TensorProto.INT64, [1, N_WORDS]),
        helper.make_tensor_value_info('word_dur', TensorProto.INT64, [1, N_WORDS]),
    ]
    outputs = [
        helper.make_tensor_value_info('encoder_out', TensorProto.FLOAT, [1, N_TOKENS, 1]),
        helper.make_tensor_value_info('x_masks', TensorProto.BOOL, [1, N_TOKENS]),
    ]

    nodes = [
        helper.make_node('Cast', ['tokens'], ['tokens_float'], to=TensorProto.FLOAT),
        helper.make_node('Unsqueeze', ['tokens_float', 'axis_2'], ['encoder_out']),
        helper.make_node('Equal', ['tokens', 'pad_token'], ['x_masks']),
    ]
    initializers = [
        scalar_initializer('axis_2', 2),
        scalar_initializer('pad_token', 0),
    ]

    graph = helper.make_graph(nodes, 'DurationWordEncoder', inputs, outputs, initializers)
    return make_model(graph)


def word_position_nodes():
    """Builds the nodes that locate every phoneme inside its word.

    A predictor that consumes the word division of the score declares C{word_div} and locates the
    word of every phoneme with it. The nodes report the membership of every phoneme in every word
    and the one based position of the phoneme inside the word it belongs to, so a model that uses
    the position alone still depends on the values of C{word_div}.
    """
    nodes = [
        # Word boundaries of the score, as a start and an end index per word.
        helper.make_node('CumSum', ['word_div', 'axis_1'], ['word_end']),
        helper.make_node('Sub', ['word_end', 'word_div'], ['word_start']),
        # Phoneme index sequence of the current utterance.
        helper.make_node('Shape', ['ph_midi'], ['input_shape']),
        helper.make_node('Gather', ['input_shape', 'index_1'], ['token_count'], axis=0),
        helper.make_node('Range', ['zero', 'token_count', 'one'], ['token_index']),
        helper.make_node('Unsqueeze', ['token_index', 'axis_1'], ['token_index_column']),
        # Membership of every phoneme in every word.
        helper.make_node('Sub', ['token_index_column', 'word_start'], ['word_offset']),
        helper.make_node('Less', ['word_offset', 'word_div'], ['before_word_end']),
        helper.make_node('Less', ['word_offset', 'zero'], ['before_word_start']),
        helper.make_node('Not', ['before_word_start'], ['after_word_start']),
        helper.make_node('And', ['before_word_end', 'after_word_start'], ['word_member']),
        helper.make_node('Cast', ['word_member'], ['word_member_float'], to=TensorProto.FLOAT),
        # Raised from the word end index and the word start index of every phoneme.
        helper.make_node('Cast', ['word_start'], ['word_start_float'], to=TensorProto.FLOAT),
        helper.make_node('Transpose', ['word_start_float'], ['word_start_column']),
        helper.make_node('MatMul', ['word_member_float', 'word_start_column'], ['word_begin']),
        helper.make_node('Cast', ['token_index'], ['token_index_float'], to=TensorProto.FLOAT),
        helper.make_node('Unsqueeze', ['token_index_float', 'axis_0'], ['token_index_row']),
        helper.make_node('Transpose', ['word_begin'], ['word_begin_row']),
        helper.make_node('Sub', ['token_index_row', 'word_begin_row'], ['word_position']),
        helper.make_node('Add', ['word_position', 'one_float'], ['word_position_one_based']),
    ]
    initializers = [
        scalar_initializer('zero', 0),
        scalar_initializer('one', 1),
        scalar_initializer('index_1', 1),
        scalar_initializer('axis_0', 0),
        scalar_initializer('axis_1', 1),
        scalar_initializer('one_float', 1.0, TensorProto.FLOAT),
    ]
    return nodes, initializers


def word_predictor_inputs(declares_budget):
    inputs = [
        helper.make_tensor_value_info('encoder_out', TensorProto.FLOAT, [1, N_TOKENS, 1]),
        helper.make_tensor_value_info('x_masks', TensorProto.BOOL, [1, N_TOKENS]),
        helper.make_tensor_value_info('ph_midi', TensorProto.INT64, [1, N_TOKENS]),
        helper.make_tensor_value_info('word_div', TensorProto.INT64, [1, N_WORDS]),
    ]
    if declares_budget:
        inputs.append(helper.make_tensor_value_info('word_dur', TensorProto.INT64, [1, N_WORDS]))
    return inputs


def make_word_predictor():
    """Builds a duration predictor that splits the frame budget of every word.

    The prediction of one phoneme is the frame budget of its word plus the one based position of the
    phoneme inside that word. The frame budget therefore changes the shares of a word, which lets
    the test check the value of C{word_dur} and not only its presence.
    """
    outputs = [
        helper.make_tensor_value_info('ph_dur_pred', TensorProto.FLOAT, [1, N_TOKENS]),
    ]

    nodes, initializers = word_position_nodes()
    nodes += [
        # Frame budget of the word of every phoneme.
        helper.make_node('Cast', ['word_dur'], ['word_dur_float'], to=TensorProto.FLOAT),
        helper.make_node('Transpose', ['word_dur_float'], ['word_dur_column']),
        helper.make_node('MatMul', ['word_member_float', 'word_dur_column'], ['word_budget']),
        helper.make_node('Transpose', ['word_budget'], ['word_budget_row']),
        helper.make_node('Add', ['word_budget_row', 'word_position_one_based'], ['ph_dur_pred']),
    ]

    graph = helper.make_graph(nodes, 'DurationWordPredictor', word_predictor_inputs(True),
                              outputs, initializers)
    return make_model(graph)


def make_division_predictor():
    """Builds a duration predictor that consumes the word division and no frame budget.

    The prediction of one phoneme is its one based position inside its word, so the shares of a word
    follow a ramp that the division alone decides. A predictor of this signature is exported when
    the duration model adds the position of a phoneme inside its word without splitting a budget.
    """
    outputs = [
        helper.make_tensor_value_info('ph_dur_pred', TensorProto.FLOAT, [1, N_TOKENS]),
    ]

    nodes, initializers = word_position_nodes()
    nodes.append(helper.make_node('Identity', ['word_position_one_based'], ['ph_dur_pred']))

    graph = helper.make_graph(nodes, 'DurationDivisionPredictor', word_predictor_inputs(False),
                              outputs, initializers)
    return make_model(graph)


def make_absolute_predictor():
    """Builds a duration predictor that predicts absolute durations.

    The prediction is the MIDI pitch of the phoneme, which is constant inside one word, so the test
    checks that the predictor takes neither word input while every word keeps its duration.
    """
    inputs = [
        helper.make_tensor_value_info('encoder_out', TensorProto.FLOAT, [1, N_TOKENS, 1]),
        helper.make_tensor_value_info('x_masks', TensorProto.BOOL, [1, N_TOKENS]),
        helper.make_tensor_value_info('ph_midi', TensorProto.INT64, [1, N_TOKENS]),
    ]
    outputs = [
        helper.make_tensor_value_info('ph_dur_pred', TensorProto.FLOAT, [1, N_TOKENS]),
    ]

    nodes = [
        helper.make_node('Cast', ['ph_midi'], ['ph_dur_pred'], to=TensorProto.FLOAT),
    ]

    graph = helper.make_graph(nodes, 'DurationAbsolutePredictor', inputs, outputs)
    return make_model(graph)


def save(model, name):
    model_path = current_directory().parents[1] / 'models'
    onnx.checker.check_model(model)
    onnx.save_model(model, model_path / name)
    print(f"ONNX model '{name}' created successfully.")


if __name__ == '__main__':
    save(make_encoder(), 'duration_word_encoder.onnx')
    save(make_word_predictor(), 'duration_word_predictor.onnx')
    save(make_division_predictor(), 'duration_division_predictor.onnx')
    save(make_absolute_predictor(), 'duration_absolute_predictor.onnx')
